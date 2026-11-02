// beatles - ReXGlue Recompiled Project
//
// Automatic RenderDoc captures of slow frames (--capture_slow_frames N).
//
// Run the game with the RenderDoc Vulkan layer loaded:
//   ENABLE_VULKAN_RENDERDOC_CAPTURE=1 scripts/run.sh --capture_slow_frames=4 ...
// When a guest frame interval exceeds --capture_slow_ms (default 24 ms, i.e. a
// missed 60 Hz vsync), the next frame is captured, at most N times and at least
// 15 s apart. Creating the file named by --capture_trigger_file (default
// build/capture_now) requests one capture right away (it is deleted), which is
// how to grab a normal frame for comparison. Captures go to
// --capture_path_template (RenderDoc appends _frameN.rdc).
//
// TriggerCapture() would capture on the next present of RenderDoc's "active
// window", but RenderDoc does not track Wayland windows, so nothing happens.
// Instead the capture is explicit: StartFrameCapture at one VdSwap and
// EndFrameCapture two swaps later (wildcard device/window). The GPU command
// thread trails the guest by about a frame, so a capture holds roughly two
// guest frames of submissions.
//
// The SDK auto-enables GPU debug markers when RenderDoc is attached, so the
// captures are annotated. The RenderDoc overlay is turned off.
//
// Radeon GPU Profiler mode (--rgp_slow_frames N): with RADV's trace layer
// (MESA_VK_TRACE=rgp MESA_VK_TRACE_TRIGGER=<file>), RADV captures the next
// presented frame whenever <file> exists. This creates that file when the game
// is in a *sustained* slow stretch (at least a third of the last 30 guest frames
// slower than --capture_slow_ms), so heavy scenes are caught rather than
// one-off loading hitches; at most N times, at least 15 s apart. RADV writes
// the traces to /tmp/<exe>_<date>_frame<N>.rgp. Add --gpu_debug_markers=true
// for labelled passes.

#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <renderdoc_app.h>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(capture_slow_frames, 0, "Debug",
                     "Capture up to N slow frames with RenderDoc (needs the RenderDoc layer, "
                     "ENABLE_VULKAN_RENDERDOC_CAPTURE=1)");
REXCVAR_DEFINE_DOUBLE(capture_slow_ms, 24.0, "Debug",
                      "Guest frame interval (ms) above which a frame counts as slow");
REXCVAR_DEFINE_STRING(capture_trigger_file, "build/capture_now", "Debug",
                      "If this file appears, capture the next frame and delete the file");
REXCVAR_DEFINE_INT32(rgp_slow_frames, 0, "Debug",
                     "Request up to N Radeon GPU Profiler traces during sustained slow stretches "
                     "(needs MESA_VK_TRACE=rgp and MESA_VK_TRACE_TRIGGER)");
REXCVAR_DEFINE_STRING(capture_path_template, "build/captures/beatles", "Debug",
                      "RenderDoc capture path template");

namespace {

using Clock = std::chrono::steady_clock;

RENDERDOC_API_1_4_1* Api() {
  static RENDERDOC_API_1_4_1* api = []() -> RENDERDOC_API_1_4_1* {
    if (REXCVAR_GET(capture_slow_frames) <= 0) return nullptr;
    void* lib = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) {
      REXLOG_WARN("capture: RenderDoc is not loaded (run with ENABLE_VULKAN_RENDERDOC_CAPTURE=1)");
      return nullptr;
    }
    auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(lib, "RENDERDOC_GetAPI"));
    RENDERDOC_API_1_4_1* out = nullptr;
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_4_1, reinterpret_cast<void**>(&out))) {
      REXLOG_WARN("capture: RENDERDOC_GetAPI failed");
      return nullptr;
    }
    out->SetCaptureFilePathTemplate(REXCVAR_GET(capture_path_template).c_str());
    out->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
    REXLOG_INFO("capture: RenderDoc ready, up to {} slow-frame captures to {}",
                REXCVAR_GET(capture_slow_frames), REXCVAR_GET(capture_path_template));
    return out;
  }();
  return api;
}

// RGP trigger: see the header comment.
void RgpOnSwap(double frame_ms) {
  static const int max_traces = REXCVAR_GET(rgp_slow_frames);
  if (max_traces <= 0) return;
  static const char* trigger = [] {
    const char* t = std::getenv("MESA_VK_TRACE_TRIGGER");
    if (!t || !*t) REXLOG_WARN("capture: --rgp_slow_frames needs MESA_VK_TRACE_TRIGGER");
    return t;
  }();
  if (!trigger || !*trigger) return;
  static std::array<bool, 30> slow{};
  static uint32_t swaps = 0, slow_count = 0;
  static int taken = 0;
  static Clock::time_point last{};
  bool& entry = slow[swaps % slow.size()];
  slow_count -= entry;
  entry = frame_ms > REXCVAR_GET(capture_slow_ms);
  slow_count += entry;
  ++swaps;
  if (taken >= max_traces || swaps < 600 || slow_count < slow.size() / 3) return;
  const Clock::time_point now = Clock::now();
  if (last != Clock::time_point{} && now - last < std::chrono::seconds(15)) return;
  last = now;
  ++taken;
  if (FILE* f = std::fopen(trigger, "w")) std::fclose(f);
  REXLOG_INFO("capture: RGP trace requested at swap {} ({} of last {} frames slow, {}/{})", swaps,
              slow_count, slow.size(), taken, max_traces);
}

}  // namespace

// Called by the VdSwap hook (perf_hooks.cpp) with the guest frame interval.
void GpuCaptureOnSwap(double frame_ms) {
  RgpOnSwap(frame_ms);
  RENDERDOC_API_1_4_1* api = Api();
  if (!api) return;
  static int slow_taken = 0;
  static Clock::time_point last_slow{};
  static uint32_t swaps = 0;
  static uint32_t end_at_swap = 0;  // non-zero while a capture is open
  ++swaps;
  if (end_at_swap) {
    if (swaps >= end_at_swap) {
      const uint32_t ok = api->EndFrameCapture(nullptr, nullptr);
      REXLOG_INFO("capture: ended at swap {} ({})", swaps, ok ? "written" : "FAILED");
      end_at_swap = 0;
    }
    return;
  }
  auto start = [&] {
    api->StartFrameCapture(nullptr, nullptr);
    end_at_swap = swaps + 2;
  };

  const std::string& trigger = REXCVAR_GET(capture_trigger_file);
  if (!trigger.empty() && access(trigger.c_str(), F_OK) == 0) {
    unlink(trigger.c_str());
    start();
    REXLOG_INFO("capture: requested by {} (swap {}, frame {:.1f} ms)", trigger, swaps, frame_ms);
    return;
  }
  const Clock::time_point now = Clock::now();
  if (slow_taken < REXCVAR_GET(capture_slow_frames) && frame_ms > REXCVAR_GET(capture_slow_ms) &&
      swaps > 600 && (last_slow == Clock::time_point{} || now - last_slow > std::chrono::seconds(15))) {
    ++slow_taken;
    last_slow = now;
    start();
    REXLOG_INFO("capture: slow frame {:.1f} ms at swap {} ({}/{})", frame_ms, swaps, slow_taken,
                REXCVAR_GET(capture_slow_frames));
  }
}
