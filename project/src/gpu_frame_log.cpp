// beatles - ReXGlue Recompiled Project
//
// Per-frame host GPU log (--gpu_frame_csv <path>, needs the rebuilt xenos plugin with
// patches/rexglue-sdk/0006).
//
// The --perf_log GPU numbers are 2 s averages; a camera cut is one or two frames. This turns
// on the plugin's timestamp queries (one pair per Vulkan submission) and writes one CSV row
// per host frame once the GPU has finished it:
//   frame          host frame index
//   cpu_close_s    when the GPU command thread closed the frame (steady clock, seconds since
//                  the first row); differences give the guest frame pace
//   gpu_start_ms   GPU clock at the frame's first submission, ms since the first row
//   gpu_ms         first submission start to last submission end (GPU timeline)
//   busy_ms        summed submission durations (the frame's own GPU work)
//   gap_ms         idle/foreign GPU time before the frame (start minus previous frame's end;
//                  presenter paints run here)
//   interval_ms    end minus previous frame's end (GPU frame pace)
//   then counters of what the frame contained (GpuStatSlot deltas, patch 0003/0006);
//   with --gpu_frame_categories, also the GPU ms spent in EDRAM dumps, resolves, texture
//   loads, render-target transfers and shared-memory uploads (timestamp pairs around each,
//   so a little overhead); the rest of busy_ms is the game's own draws and post passes.
// A frame that costs more than ~16.7 ms of busy time cannot make 60 fps by itself.
// Analyse with scripts/analyze_gpu_frames.py.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(gpu_frame_csv, "", "Debug",
                      "Write per-frame host GPU times and command counters to this CSV file "
                      "(needs the rebuilt xenos plugin, scripts/build_sdk.sh)");
REXCVAR_DEFINE_BOOL(gpu_frame_categories, false, "Debug",
                    "With --gpu_frame_csv, also time EDRAM dumps, resolves, texture loads, "
                    "render-target transfers and uploads separately");

// present_hooks.cpp
void* GpuPluginSymbol(const char* name);

namespace {

// Must match beatles_frame_timer::FrameRecord in patches/rexglue-sdk/0006.
struct FrameRecord {
  uint64_t frame;
  uint64_t gpu_start_ns;
  uint64_t gpu_end_ns;
  uint64_t busy_ns;
  uint64_t cpu_close_ns;
  uint32_t submissions;
  uint32_t stat_count;
  uint64_t stats[48];
  uint64_t category_ns[8];  // GpuTimeCategory order: dump, resolve, texload, transfer, upload
  uint32_t category_spans[8];
};

using EnableFn = void (*)();
using TakeFn = size_t (*)(void*, size_t);

// Indices into FrameRecord::stats: DeferredCommandBuffer::Command values, then GpuStatSlot.
enum : size_t {
  kBeginRenderPass = 0,
  kClearAttachments = 6,
  kClearColorImage = 7,
  kCopyBuffer = 8,
  kCopyBufferToImage = 9,
  kDispatch = 11,
  kDraw = 12,
  kDrawIndexed = 13,
  kBeginRendering = 16,
  kPipelineBarrier = 18,
  kUploads = 29,
  kUploadBytes = 30,
  kUploadRenderPassEnds = 31,
  kGuestDraws = 33,
  kResolves = 34,
  kEdramDumps = 35,
  kTextureLoads = 36,
  kTextureLoadDispatches = 37,
  kTextureLoadBytes = 38,
  kRtTransferDraws = 39,
  kStatsNeeded = 40,
};

void WriterThread(TakeFn take, FILE* f) {
  static FrameRecord records[256];
  bool have_origin = false;
  uint64_t origin_cpu = 0, origin_gpu = 0, prev_end = 0;
  uint64_t frames = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    size_t n;
    while ((n = take(records, std::size(records))) != 0) {
      for (size_t i = 0; i < n; ++i) {
        const FrameRecord& r = records[i];
        if (!have_origin) {
          have_origin = true;
          origin_cpu = r.cpu_close_ns;
          origin_gpu = r.gpu_start_ns;
          prev_end = r.gpu_start_ns;
        }
        const uint64_t* v = r.stats;
        const bool stats = r.stat_count >= kStatsNeeded;
        auto st = [&](size_t i) -> unsigned long long { return stats ? v[i] : 0; };
        const double gap = (double(r.gpu_start_ns) - double(prev_end)) / 1e6;
        const double interval = (double(r.gpu_end_ns) - double(prev_end)) / 1e6;
        std::fprintf(
            f,
            "%llu,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%llu,%llu,%llu,%llu,%llu,%llu,%.1f,%llu,%llu,"
            "%llu,%.1f,%llu,%llu,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            (unsigned long long)r.frame,
            r.cpu_close_ns ? (double(r.cpu_close_ns) - double(origin_cpu)) / 1e9 : -1.0,
            (double(r.gpu_start_ns) - double(origin_gpu)) / 1e6,
            (double(r.gpu_end_ns) - double(r.gpu_start_ns)) / 1e6, double(r.busy_ns) / 1e6, gap,
            interval, r.submissions, st(kGuestDraws), st(kDraw) + st(kDrawIndexed),
            st(kBeginRenderPass) + st(kBeginRendering), st(kPipelineBarrier), st(kDispatch),
            st(kUploads), st(kUploadBytes) / 1024.0, st(kUploadRenderPassEnds),
            st(kTextureLoads), st(kTextureLoadDispatches), st(kTextureLoadBytes) / 1024.0,
            st(kResolves), st(kEdramDumps), st(kRtTransferDraws),
            st(kClearAttachments) + st(kClearColorImage) + st(kCopyBufferToImage),
            r.category_ns[0] / 1e6, r.category_ns[1] / 1e6, r.category_ns[2] / 1e6,
            r.category_ns[3] / 1e6, r.category_ns[4] / 1e6);
        prev_end = r.gpu_end_ns;
        ++frames;
      }
      std::fflush(f);
    }
  }
}

}  // namespace

// Called by perf_hooks.cpp on every VdSwap; starts the log once the plugin is loaded.
void GpuFrameLogOnSwap() {
  static std::atomic<bool> started{false};
  if (started.load(std::memory_order_relaxed)) return;
  static const std::string path = REXCVAR_GET(gpu_frame_csv);
  if (path.empty()) {
    started = true;
    return;
  }
  started = true;
  auto enable = reinterpret_cast<EnableFn>(GpuPluginSymbol("rex_gpu_frame_timing_enable"));
  auto take = reinterpret_cast<TakeFn>(GpuPluginSymbol("rex_gpu_take_frame_records"));
  if (!enable || !take) {
    REXLOG_WARN("gpu_frame_csv: the xenos plugin lacks patch 0006 (run scripts/build_sdk.sh)");
    return;
  }
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) {
    REXLOG_WARN("gpu_frame_csv: cannot open {}", path);
    return;
  }
  std::fprintf(f,
               "frame,cpu_close_s,gpu_start_ms,gpu_ms,busy_ms,gap_ms,interval_ms,submissions,"
               "guest_draws,draws,render_passes,barriers,dispatches,uploads,upload_kb,"
               "upload_rp_ends,tex_loads,tex_dispatches,tex_kb,resolves,edram_dumps,"
               "rt_transfer_draws,clears_and_image_copies,dump_ms,resolve_ms,texload_ms,transfer_ms,"
               "upload_ms\n");
  enable();
  if (REXCVAR_GET(gpu_frame_categories)) {
    if (auto categories =
            reinterpret_cast<EnableFn>(GpuPluginSymbol("rex_gpu_category_timing_enable"))) {
      categories();
    }
  }
  std::thread(WriterThread, take, f).detach();
  REXLOG_INFO("gpu_frame_csv: writing per-frame GPU times to {}", path);
}
