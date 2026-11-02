// beatles - ReXGlue Recompiled Project
//
// Guest frame-rate counter for performance work.
//
// The game presents every frame through the VdSwap kernel import, so counting
// VdSwap calls gives the guest frame rate independently of host presentation
// (vsync, skipped frames). The import is defined here, in the executable, and
// forwards to the SDK implementation found with dlsym(RTLD_NEXT).
//
// Outputs (all fed by the same counter, refreshed every 0.5 s):
//   --show_fps true   small always-on overlay in the top-left corner;
//   F3                the SDK debug overlay ("Guest: N FPS"), via SetGuestFrameStats;
//   --perf_log true   a log line every 2 s (also host GPU busy % and clock, amdgpu sysfs):
//                     perf: 23.5 fps (47 swaps in 2.00 s), worst frame 61.2 ms, audio 187.5 frames/s
//
// The audio rate counts XAudioSubmitRenderDriverFrame calls. Each frame is 256
// samples at 48 kHz, so real-time playback needs 187.5 frames/s; Harmonix games
// derive song time from audio playback, so a lower rate means slow motion.
//
// --audio_dump <path> appends every submitted frame to <path> as raw guest data
// (6 channels x 256 samples, channel-major, big-endian float32) for offline
// analysis (gaps of silence, pitch); see scripts/analyze_audio_dump.py.

#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <fmt/format.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/debug_overlay.h>

REXCVAR_DEFINE_BOOL(perf_log, false, "Debug",
                    "Log the guest frame rate (VdSwap calls per second) every 2 seconds");
REXCVAR_DEFINE_STRING(audio_dump, "", "Debug",
                      "Append every audio frame sent to the host to this file (raw guest "
                      "format: 6 x 256 big-endian float32, channel-major)");
REXCVAR_DEFINE_BOOL(show_fps, false, "Debug",
                    "Show the guest frame rate in a small overlay in the top-left corner");

// present_hooks.cpp: host mailbox/paint counters, reset on each call.
std::string PresentStatsLine();
// present_hooks.cpp: blocks until the previous frame was painted (--present_pacing).
void PacePresent();
// gpu_capture.cpp: RenderDoc captures of slow frames (--capture_slow_frames).
void GpuCaptureOnSwap(double frame_ms);
// gpu_frame_log.cpp: per-frame host GPU times (--gpu_frame_csv).
void GpuFrameLogOnSwap();
// hitch_tracer.cpp
void HitchOnSwap(std::chrono::steady_clock::time_point prev,
                 std::chrono::steady_clock::time_point now, double prev_call_ms);

namespace {

using GuestImport = void (*)(PPCContext&, uint8_t*);
using Clock = std::chrono::steady_clock;

// Accumulates frame intervals over a window and publishes a summary when the
// window is at least `period_s` long.
struct Window {
  Clock::time_point start{};
  uint32_t swaps = 0;
  double worst_ms = 0.0;

  // Returns true (and fills the outputs) when the window closed.
  bool Add(Clock::time_point now, double frame_ms, double period_s, double& fps, double& worst,
           uint32_t& count, double& seconds) {
    ++swaps;
    if (frame_ms > worst_ms) worst_ms = frame_ms;
    seconds = std::chrono::duration<double>(now - start).count();
    if (seconds < period_s) return false;
    fps = swaps / seconds;
    worst = worst_ms;
    count = swaps;
    start = now;
    swaps = 0;
    worst_ms = 0.0;
    return true;
  }
};

// Host GPU load (amdgpu sysfs), sampled every 50 ms by a background thread
// while --perf_log is on; the perf line reports and resets the window.
struct GpuSampler {
  std::mutex mutex;
  uint32_t samples = 0, busy_sum = 0, busy_max = 0, saturated = 0;  // saturated: >= 95%
  uint64_t sclk_sum = 0;
  uint32_t sclk_samples = 0, sclk_min = 0;
  int temp_max_mc = 0;      // hwmon temp1 (edge), millidegrees C
  uint64_t ppt_sum_uw = 0;  // hwmon power1 (APU package power, CPU + GPU), microwatts
  uint64_t ppt_max_uw = 0;
  uint32_t ppt_samples = 0;
};

// Reads one integer from a sysfs file; returns false if unreadable.
bool ReadSysInt(const std::string& path, long long& value) {
  FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "r");
  if (!f) return false;
  const bool ok = std::fscanf(f, "%lld", &value) == 1;
  std::fclose(f);
  return ok;
}

// Returns <device>/hwmon/hwmonN/<file> for the first hwmon of the GPU device.
std::string FindHwmonFile(const std::string& device_file, const char* file) {
  const std::string dir = device_file.substr(0, device_file.rfind('/')) + "/hwmon/";
  for (int i = 0; i < 16; ++i) {
    std::string path = dir + "hwmon" + std::to_string(i) + "/" + file;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
      std::fclose(f);
      return path;
    }
  }
  return {};
}

GpuSampler& Gpu() {
  static GpuSampler g;
  return g;
}

// Returns the first readable /sys/class/drm/cardN/device/<file>, or "".
std::string FindDrmFile(const char* file) {
  for (int i = 0; i < 8; ++i) {
    std::string path = "/sys/class/drm/card" + std::to_string(i) + "/device/" + file;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
      std::fclose(f);
      return path;
    }
  }
  return {};
}

void StartGpuSampler() {
  const std::string busy_path = FindDrmFile("gpu_busy_percent");
  if (busy_path.empty()) return;
  const std::string sclk_path = FindDrmFile("pp_dpm_sclk");
  const std::string temp_path = FindHwmonFile(busy_path, "temp1_input");
  const std::string ppt_path = FindHwmonFile(busy_path, "power1_input");
  std::thread([busy_path, sclk_path, temp_path, ppt_path] {
    char line[128];
    for (;;) {
      int busy = -1;
      if (FILE* f = std::fopen(busy_path.c_str(), "r")) {
        if (std::fscanf(f, "%d", &busy) != 1) busy = -1;
        std::fclose(f);
      }
      // pp_dpm_sclk lists the DPM levels; the active one ends with '*'.
      int sclk = -1;
      if (FILE* f = sclk_path.empty() ? nullptr : std::fopen(sclk_path.c_str(), "r")) {
        while (std::fgets(line, sizeof(line), f)) {
          int level, mhz;
          if (std::strchr(line, '*') && std::sscanf(line, "%d: %dMhz", &level, &mhz) == 2) {
            sclk = mhz;
          }
        }
        std::fclose(f);
      }
      long long temp = -1, ppt = -1;
      ReadSysInt(temp_path, temp);
      ReadSysInt(ppt_path, ppt);
      if (busy >= 0) {
        GpuSampler& g = Gpu();
        if (temp > g.temp_max_mc) g.temp_max_mc = int(temp);
        if (ppt >= 0) {
          g.ppt_sum_uw += ppt;
          if (uint64_t(ppt) > g.ppt_max_uw) g.ppt_max_uw = ppt;
          ++g.ppt_samples;
        }
        std::lock_guard<std::mutex> guard(g.mutex);
        ++g.samples;
        g.busy_sum += busy;
        if (uint32_t(busy) > g.busy_max) g.busy_max = busy;
        if (busy >= 95) ++g.saturated;
        if (sclk > 0) {
          g.sclk_sum += sclk;
          if (!g.sclk_samples || uint32_t(sclk) < g.sclk_min) g.sclk_min = sclk;
          ++g.sclk_samples;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }).detach();
}

std::string GpuStatsLine() {
  GpuSampler& g = Gpu();
  std::lock_guard<std::mutex> guard(g.mutex);
  if (!g.samples) return "gpu: n/a";
  std::string out = fmt::format("gpu busy avg {}% max {}%, >=95% in {}% of samples", g.busy_sum / g.samples,
                                g.busy_max, g.saturated * 100 / g.samples);
  if (g.sclk_samples) {
    out += fmt::format(", sclk avg {} min {} MHz", g.sclk_sum / g.sclk_samples, g.sclk_min);
  }
  if (g.temp_max_mc > 0) out += fmt::format(", edge max {} C", g.temp_max_mc / 1000);
  if (g.ppt_samples) {
    out += fmt::format(", ppt avg {:.1f} max {:.1f} W", g.ppt_sum_uw / 1e6 / g.ppt_samples,
                       g.ppt_max_uw / 1e6);
  }
  g.samples = g.busy_sum = g.busy_max = g.saturated = g.sclk_samples = g.sclk_min = 0;
  g.ppt_samples = 0;
  g.sclk_sum = g.ppt_sum_uw = g.ppt_max_uw = 0;
  g.temp_max_mc = 0;
  return out;
}

std::atomic<uint64_t> g_audio_frames{0};
// Audio submit pacing: submits that came less than 1 ms after the previous one
// (bursts) and the longest gap, reset by each --perf_log line.
std::atomic<uint64_t> g_audio_burst{0};
std::atomic<int64_t> g_audio_max_gap_us{0};

struct FrameCounter {
  std::mutex mutex;
  Clock::time_point last_swap{};
  uint64_t total_swaps = 0;
  Window display;  // 0.5 s, for the overlays
  Window log;      // 2 s, for --perf_log
  uint64_t log_audio_frames = 0;
  // Swap intervals in the --perf_log window: <10, 10-23, 23-40, 40+ ms.
  uint32_t log_buckets[4] = {};
  // Last published display values.
  double fps = 0.0;
  double worst_ms = 0.0;
};

FrameCounter& Counter() {
  static FrameCounter counter;
  return counter;
}

void CountSwap() {
  FrameCounter& c = Counter();
  const Clock::time_point now = Clock::now();
  std::lock_guard<std::mutex> guard(c.mutex);
  ++c.total_swaps;
  if (c.last_swap == Clock::time_point{}) {
    c.last_swap = c.display.start = c.log.start = now;
    if (REXCVAR_GET(perf_log)) StartGpuSampler();
    return;
  }
  const double frame_ms = std::chrono::duration<double, std::milli>(now - c.last_swap).count();
  c.last_swap = now;
  ++c.log_buckets[frame_ms < 10.0 ? 0 : frame_ms < 23.0 ? 1 : frame_ms < 40.0 ? 2 : 3];

  double fps, worst, seconds;
  uint32_t count;
  if (c.display.Add(now, frame_ms, 0.5, fps, worst, count, seconds)) {
    c.fps = fps;
    c.worst_ms = worst;
  }
  static const bool log_enabled = REXCVAR_GET(perf_log);
  if (log_enabled && c.log.Add(now, frame_ms, 2.0, fps, worst, count, seconds)) {
    const uint64_t audio = g_audio_frames.load(std::memory_order_relaxed);
    const double audio_rate = (audio - c.log_audio_frames) / seconds;
    c.log_audio_frames = audio;
    const uint64_t bursts = g_audio_burst.exchange(0, std::memory_order_relaxed);
    const int64_t max_gap = g_audio_max_gap_us.exchange(0, std::memory_order_relaxed);
    REXLOG_INFO(
        "perf: {:.1f} fps ({} swaps in {:.2f} s), worst frame {:.1f} ms, audio {:.1f} frames/s "
        "({} back-to-back, max gap {:.1f} ms); swap intervals <10/10-23/23-40/40+ ms: "
        "{}/{}/{}/{}; {}; {}",
        fps, count, seconds, worst, audio_rate, bursts, max_gap / 1000.0, c.log_buckets[0],
        c.log_buckets[1], c.log_buckets[2], c.log_buckets[3], PresentStatsLine(), GpuStatsLine());
    for (uint32_t& b : c.log_buckets) b = 0;
  }
}

struct Snapshot {
  double fps = 0.0;
  double worst_ms = 0.0;
  uint64_t total_swaps = 0;
};

Snapshot Read() {
  FrameCounter& c = Counter();
  std::lock_guard<std::mutex> guard(c.mutex);
  Snapshot s{c.fps, c.worst_ms, c.total_swaps};
  // If the game stops swapping (hang, loading), show the time since the last
  // frame instead of a stale rate.
  if (c.last_swap != Clock::time_point{}) {
    const double since_s = std::chrono::duration<double>(Clock::now() - c.last_swap).count();
    if (since_s > 1.0) {
      s.fps = 0.0;
      s.worst_ms = since_s * 1000.0;
    }
  }
  return s;
}

class FpsOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit FpsOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    const Snapshot s = Read();
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##beatles_fps", nullptr, kFlags)) {
      ImGui::SetWindowFontScale(1.6f);  // the default ImGui font is tiny at 1080p
      if (s.total_swaps < 2) {
        ImGui::TextUnformatted("FPS: --");
      } else {
        const double avg_ms = s.fps > 0.0 ? 1000.0 / s.fps : 0.0;
        ImGui::Text("%.1f FPS  %.1f ms  (worst %.0f ms)", s.fps, avg_ms, s.worst_ms);
      }
    }
    ImGui::End();
  }
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateFpsOverlay(rex::ui::ImGuiDrawer* drawer) {
  if (!drawer || !REXCVAR_GET(show_fps)) return nullptr;
  return std::make_unique<FpsOverlay>(drawer);
}

rex::ui::FrameStats GuestFrameStats() {
  const Snapshot s = Read();
  rex::ui::FrameStats stats;
  stats.fps = s.fps;
  stats.frame_time_ms = s.fps > 0.0 ? 1000.0 / s.fps : 0.0;
  stats.frame_count = s.total_swaps;
  return stats;
}

namespace {

GuestImport ResolveSdk(const char* name) {
  auto fn = reinterpret_cast<GuestImport>(dlsym(RTLD_NEXT, name));
  if (!fn) {
    std::fprintf(stderr, "perf_hooks: cannot resolve %s in the SDK: %s\n", name, dlerror());
    std::abort();
  }
  return fn;
}

}  // namespace

REX_EXTERN(__imp__VdSwap) {
  static const GuestImport sdk_impl = ResolveSdk("__imp__VdSwap");
  static Clock::time_point prev{};
  static double prev_call_ms = 0.0;
  const Clock::time_point now = Clock::now();
  HitchOnSwap(prev, now, prev_call_ms);
  GpuFrameLogOnSwap();
  if (prev != Clock::time_point{}) {
    GpuCaptureOnSwap(std::chrono::duration<double, std::milli>(now - prev).count());
  }
  prev = now;
  CountSwap();
  sdk_impl(ctx, base);
  PacePresent();
  prev_call_ms = std::chrono::duration<double, std::milli>(Clock::now() - now).count();
}

REX_EXTERN(__imp__XAudioSubmitRenderDriverFrame) {
  static const GuestImport sdk_impl = ResolveSdk("__imp__XAudioSubmitRenderDriverFrame");
  g_audio_frames.fetch_add(1, std::memory_order_relaxed);
  {
    static Clock::time_point last{};
    const Clock::time_point now = Clock::now();
    if (last != Clock::time_point{}) {
      const int64_t gap_us =
          std::chrono::duration_cast<std::chrono::microseconds>(now - last).count();
      if (gap_us < 1000) g_audio_burst.fetch_add(1, std::memory_order_relaxed);
      int64_t prev = g_audio_max_gap_us.load(std::memory_order_relaxed);
      while (gap_us > prev &&
             !g_audio_max_gap_us.compare_exchange_weak(prev, gap_us, std::memory_order_relaxed)) {
      }
    }
    last = now;
  }
  static FILE* dump = [] {
    const std::string path = REXCVAR_GET(audio_dump);
    return path.empty() ? nullptr : std::fopen(path.c_str(), "wb");
  }();
  if (dump && ctx.r4.u32) {
    constexpr size_t kFrameBytes = 6 * 256 * sizeof(float);
    std::fwrite(base + ctx.r4.u32, 1, kFrameBytes, dump);
  }
  sdk_impl(ctx, base);
}

// XAudioRegisterRenderDriverClient(callback_ptr r3 -> {callback, arg}, driver_ptr r4)
// Logs the guest callback the SDK audio worker will call once per consumed frame.
REX_EXTERN(__imp__XAudioRegisterRenderDriverClient) {
  static const GuestImport sdk_impl = ResolveSdk("__imp__XAudioRegisterRenderDriverClient");
  const uint32_t callback_ptr = ctx.r3.u32;
  if (callback_ptr) {
    auto be32 = [&](uint32_t addr) { return __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + addr)); };
    REXLOG_INFO("XAudioRegisterRenderDriverClient: callback={:08X} arg={:08X}", be32(callback_ptr),
                be32(callback_ptr + 4));
  }
  sdk_impl(ctx, base);
}
