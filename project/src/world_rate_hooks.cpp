// world_rate_hooks.cpp - optionally render the 3D venue at the full frame rate.
//
// The Milo engine splits world rendering across frames on purpose. Every frame,
// Rnd::BeginDrawing asks the ProcCounter (TheRnd + 0x118) which parts to process:
// kProcessWorld = 1 (venue), kProcessPost = 2 (post-processing/composite),
// kProcessChar = 4, kProcessAll = 7. In this game the counter is driven by the
// current RndPostProc's "emulate_fps" property (RndPostProc::sCurrent + 0x158,
// Milo default 30.0): the cycle length is round(120 / emulate_fps) half-frames,
// so 30 fps draws the venue every other frame and ~24 fps ("film") alternates
// cycles of 2 and 3 frames. The note highway and HUD are drawn every frame.
//
// ProcCounter::ProcCommands is the guest function at 0x8241BFC0 (Ghidra:
// Function_8241BFC0; the RB3 decomp has a simpler, non-adaptive version in
// system/rndobj/PostProc.cpp). A [[entrypoint.midasm_hook]] at its first
// instruction calls BeatlesProcCommandsHook; when --full_rate_world is set, the
// hook returns kProcessAll in r3 and the guest function returns immediately, so
// world, characters and post-processing are drawn every frame. This doubles the
// venue's GPU cost per frame.
//
// On the Vega 6 that does not fit 16.7 ms in the heavy shots (crowd, sunflower field,
// fly-throughs), and a late frame there drags the note highway down with it. By default
// (--full_rate_world_adaptive) the hook therefore only forces full rate while it fits:
// it reads the real GPU time of every host frame (timestamp queries, SDK patch 0006,
// gpu_frame_log.cpp) and the guest frame interval, and
//   - at full rate, falls back to the engine's own cadence when 2 of the last 8 frames
//     went over --full_rate_world_budget_ms of GPU time, or took over 20 ms while GPU time
//     was within 3 ms of the budget (slow frames with a light GPU are not the venue's fault);
//   - at the engine's cadence, the frames that draw the venue cost about what a full-rate
//     frame would, so it returns to full rate once those stayed 1.5 ms under the budget
//     for half a second and the hold time has passed. The hold starts at 1 s and doubles
//     (up to 8 s) each time full rate gave up again within 2 s, so a heavy shot does not
//     flip back and forth.
// Without patch 0006 in the plugin, adaptive mode uses only the guest frame interval.

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(full_rate_world, false, "Beatles",
                    "Render the 3D venue every frame instead of at the engine's emulated "
                    "film rate (RndPostProc emulate_fps; usually every other frame)");

REXCVAR_DEFINE_BOOL(full_rate_world_adaptive, true, "Beatles",
                    "With --full_rate_world, only draw the venue every frame while that fits the "
                    "frame budget; fall back to the engine's own rate in heavy shots");
REXCVAR_DEFINE_DOUBLE(full_rate_world_budget_ms, 16.5, "Beatles",
                      "Adaptive full-rate venue: GPU ms per frame above which full rate gives up");

REXCVAR_DEFINE_BOOL(venue_no_bloom, false, "Beatles",
                    "Turn off the current post-processor's bloom (written every frame)");
REXCVAR_DEFINE_BOOL(venue_no_trails, false, "Beatles",
                    "Turn off the post-processor's previous-frame effects (video trails, "
                    "motion blur, hall of time), written every frame");

// debug_probe.cpp: guest memory base, known once an input hook has run.
uint8_t* GuestBaseOrNull();
// gpu_frame_log.cpp
bool GpuFrameTimingEnable();
void GpuFramePump();

namespace {

constexpr uint64_t kProcessAll = 7;
constexpr uint32_t kPostProcCurrentPtr = 0x82ACC198;  // RndPostProc::sCurrent
constexpr uint32_t kEmulateFpsOffset = 0x158;         // RndPostProc::mEmulateFPS (float)

// RndPostProc fields (offsets from RndPostProc::SyncProperty at 0x8241DA58 in this build;
// the Xbox NgPostProc::DoPost at 0x82444588 gates each stage on them).
constexpr uint32_t kBloomColorAlpha = 0x38;   // bloom_color.a
constexpr uint32_t kBloomIntensity = 0x40;    // bloom_intensity
constexpr uint32_t kTrailDuration = 0x144;    // trail_duration
constexpr uint32_t kHallOfTimeRate = 0x168;   // hall_of_time_rate
constexpr uint32_t kMotionBlurBlend = 0x190;  // motion_blur_blend

// Venue detail experiments: zero post-processing stages on the current post-processor every
// frame (before drawing), which survives shot changes and post-processor blends.
void ApplyPostProcOverrides() {
  const bool no_bloom = REXCVAR_GET(venue_no_bloom);
  const bool no_trails = REXCVAR_GET(venue_no_trails);
  if (!no_bloom && !no_trails) return;
  uint8_t* base = GuestBaseOrNull();
  if (!base) return;
  uint32_t pp = std::byteswap(*reinterpret_cast<const uint32_t*>(base + kPostProcCurrentPtr));
  if (pp < 0x40000000u || pp >= 0x90000000u) return;
  auto zero = [&](uint32_t offset) { std::memset(base + pp + offset, 0, 4); };
  if (no_bloom) {
    zero(kBloomColorAlpha);
    zero(kBloomIntensity);
  }
  if (no_trails) {
    zero(kTrailDuration);
    zero(kHallOfTimeRate);
    zero(kMotionBlurBlend);
  }
}

// Logs every change of the current post-processor's emulate_fps, i.e. the rate the
// engine would draw the venue at (only the main thread calls ProcCommands).
void LogEmulateFpsChange() {
  static uint32_t last_pp = 0xFFFFFFFFu;
  static uint32_t last_bits = 0xFFFFFFFFu;
  const uint8_t* base = GuestBaseOrNull();
  if (!base) {
    return;
  }
  uint32_t pp = std::byteswap(*reinterpret_cast<const uint32_t*>(base + kPostProcCurrentPtr));
  uint32_t bits = 0;
  if (pp >= 0x40000000u && pp < 0x90000000u) {
    bits = std::byteswap(*reinterpret_cast<const uint32_t*>(base + pp + kEmulateFpsOffset));
  }
  if (pp == last_pp && bits == last_bits) {
    return;
  }
  last_pp = pp;
  last_bits = bits;
  float fps;
  std::memcpy(&fps, &bits, sizeof(fps));
  REXLOG_INFO("world_rate: postproc={:08X} emulate_fps={:.2f}{}", pp, fps,
              REXCVAR_GET(full_rate_world) ? " (overridden: full rate)" : "");
}

using Clock = std::chrono::steady_clock;

double Ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

// Adaptive full-rate state. GPU records arrive on the guest main thread (GpuFramePump in the
// hook) and on the CSV thread, hence the mutex.
struct Adaptive {
  std::mutex mutex;
  bool full = true;
  Clock::time_point switched{};
  Clock::time_point hold_until{};
  double hold_ms = 1000.0;
  // GPU busy ms of recent host frames, tagged with arrival time. Records lag the CPU by a
  // couple of frames, so samples from the first 60 ms after a switch are not trusted.
  std::deque<std::pair<Clock::time_point, double>> gpu;
  std::deque<double> intervals;  // guest frame intervals at full rate
  Clock::time_point last_call{};
  // Stats for the periodic log.
  Clock::time_point window_start{};
  double full_ms = 0.0, half_ms = 0.0;
  uint32_t to_half = 0, to_full = 0;
};

Adaptive& A() {
  static Adaptive a;
  return a;
}

constexpr double kSettleMs = 60.0;
constexpr double kSlowIntervalMs = 20.0;
constexpr size_t kWindow = 8;

// Returns true when the venue should be drawn this frame (kProcessAll).
bool AdaptiveWantsFullRate(bool timing) {
  Adaptive& a = A();
  const Clock::time_point now = Clock::now();
  std::lock_guard<std::mutex> guard(a.mutex);
  const double budget = REXCVAR_GET(full_rate_world_budget_ms);
  if (a.window_start == Clock::time_point{}) {
    a.window_start = now;
    a.switched = now;
  }
  double interval = 0.0;
  if (a.last_call != Clock::time_point{}) {
    interval = Ms(now - a.last_call);
    // A long gap (loading, pause, the option just switched on) is not a slow frame.
    if (interval > 250.0) interval = 0.0;
    (a.full ? a.full_ms : a.half_ms) += interval;
  }
  a.last_call = now;
  const Clock::time_point settled = a.switched + std::chrono::milliseconds(int(kSettleMs));
  while (!a.gpu.empty() && Ms(now - a.gpu.front().first) > 1000.0) a.gpu.pop_front();

  auto switch_to = [&](bool full, const char* why, double value) {
    const double lasted = Ms(now - a.switched);
    if (!full) {
      // Gave up quickly: hold the engine's cadence longer before the next try.
      a.hold_ms = lasted < 2000.0 ? std::min(a.hold_ms * 2.0, 8000.0) : 1000.0;
      a.hold_until = now + std::chrono::milliseconds(int(a.hold_ms));
      ++a.to_half;
    } else {
      ++a.to_full;
    }
    REXLOG_INFO("world_rate: adaptive -> {} ({} {:.1f} ms, after {:.0f} ms)",
                 full ? "full" : "engine rate", why, value, lasted);
    a.full = full;
    a.switched = now;
    a.intervals.clear();
  };

  if (a.full) {
    if (now >= settled && interval > 0.0) {
      a.intervals.push_back(interval);
      if (a.intervals.size() > kWindow) a.intervals.pop_front();
    }
    size_t slow_gpu = 0, seen = 0;
    double worst_gpu = 0.0;
    for (auto it = a.gpu.rbegin(); it != a.gpu.rend() && seen < kWindow; ++it) {
      if (it->first < settled) break;
      ++seen;
      worst_gpu = std::max(worst_gpu, it->second);
      if (it->second > budget) ++slow_gpu;
    }
    size_t slow_cpu = 0;
    double worst_interval = 0.0;
    for (double v : a.intervals) {
      worst_interval = std::max(worst_interval, v);
      if (v > kSlowIntervalMs) ++slow_cpu;
    }
    // Slow frames with a light GPU (video decoding, loading, CPU-side stalls) are not
    // helped by the engine's cadence, so the interval only counts when GPU time is near
    // the budget too (or unknown).
    const bool gpu_near_budget = !timing || worst_gpu > budget - 3.0;
    if (slow_gpu >= 2) {
      switch_to(false, "gpu", worst_gpu);
    } else if (slow_cpu >= 2 && gpu_near_budget) {
      switch_to(false, "frame interval", worst_interval);
    }
  } else if (now >= a.hold_until) {
    // The engine's own cadence: the venue frames among these show what full rate costs.
    size_t samples = 0;
    double worst_gpu = 0.0;
    for (auto it = a.gpu.rbegin(); it != a.gpu.rend(); ++it) {
      if (it->first < settled || Ms(now - it->first) > 500.0) break;
      ++samples;
      worst_gpu = std::max(worst_gpu, it->second);
    }
    if (!timing) {
      switch_to(true, "hold over, no gpu timing", 0.0);
    } else if (samples >= 20 && worst_gpu <= budget - 1.5) {
      switch_to(true, "gpu", worst_gpu);
    }
  }

  if (Ms(now - a.window_start) >= 10000.0) {
    const double total = a.full_ms + a.half_ms;
    if (total > 0.0) {
      REXLOG_INFO("world_rate: adaptive full rate {:.0f}% of the last {:.0f} s ({} drops to the "
                  "engine rate, {} returns; hold {:.0f} ms)",
                  100.0 * a.full_ms / total, total / 1000.0, a.to_half, a.to_full, a.hold_ms);
    }
    a.window_start = now;
    a.full_ms = a.half_ms = 0.0;
    a.to_half = a.to_full = 0;
  }
  return a.full;
}

}  // namespace

void WorldRateOnGpuFrame(uint64_t, double busy_ms) {
  Adaptive& a = A();
  std::lock_guard<std::mutex> guard(a.mutex);
  a.gpu.emplace_back(Clock::now(), busy_ms);
  if (a.gpu.size() > 256) a.gpu.pop_front();
}

// Called at the entry of ProcCounter::ProcCommands (0x8241BFC0); r3 = this.
// Returning true makes the guest function return with r3 as its result.
bool BeatlesProcCommandsHook(PPCRegister& r3) {
  LogEmulateFpsChange();
  ApplyPostProcOverrides();
  if (!REXCVAR_GET(full_rate_world)) {
    return false;
  }
  if (REXCVAR_GET(full_rate_world_adaptive)) {
    static const bool timing = GpuFrameTimingEnable();
    GpuFramePump();
    if (!AdaptiveWantsFullRate(timing)) {
      return false;  // the engine's own ProcCounter decides this frame
    }
  }
  r3.u64 = kProcessAll;
  return true;
}
