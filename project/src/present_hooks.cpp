// beatles - ReXGlue Recompiled Project
//
// Host presentation counters for frame-pacing work (KI-14).
//
// The guest frame rate (VdSwap, perf_hooks.cpp) says nothing about how many of
// those frames the host actually puts on screen. Between the two sits the SDK
// presenter's guest output mailbox (3 images):
//
//   GPU command thread: IssueSwap -> Presenter::RefreshGuestOutput   (producer)
//   UI thread paint:    VulkanPresenter::PaintAndPresentImpl
//                         -> Presenter::ConsumeGuestOutput           (consumer)
//
// A frame refreshed into the mailbox and replaced before any paint consumed it
// is dropped; a paint that finds no new image re-presents the previous one.
// Both functions are non-virtual and reached through the GOT (the GPU plugin
// calls RefreshGuestOutput across libraries; the runtime is built with
// -fno-plt, so its own ConsumeGuestOutput call goes through a GLOB_DAT slot),
// so defining them here preempts the SDK's copies; the originals are found
// with dlsym(RTLD_NEXT) and called through a member function pointer.
//
// PresentStatsLine() returns and resets the counters; perf_hooks.cpp appends
// it to each --perf_log line.
//
// Present pacing (--present_pacing true): the guest paces itself on the SDK's
// "GPU VSync" thread, a free-running 60 Hz timer unrelated to the display's
// real refresh. With a vsynced present mode the two clocks drift in phase, and
// whenever two guest frames reach the mailbox within one display refresh the
// older one is dropped and a frame is shown twice (judder with every counter
// at 60). PacePresent(), called by the VdSwap hook after the SDK queued the
// swap, blocks the guest until the previous frame has been painted, so the
// guest is never more than one frame ahead of the screen and settles onto the
// display's vsync. Frames that never reach the mailbox or are never painted
// are absorbed by resynchronizing on timeout. --present_pacing_frames=2 lets
// the guest run one more frame ahead (triple buffering): when the GPU needs a
// bit more than one refresh per frame, it then never idles waiting for vsync,
// at the cost of one frame of latency.

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>

#include <condition_variable>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_BOOL(present_pacing, false, "Debug",
                    "Block VdSwap until the previous guest frame was painted, locking the guest "
                    "frame rate to the display refresh");
REXCVAR_DEFINE_INT32(present_pacing_frames, 1, "Debug",
                     "With --present_pacing, how many guest frames may be waiting to be painted "
                     "(1 = double buffering, lowest latency; 2 = triple buffering, keeps the GPU "
                     "busy when frames miss vsync)");

namespace {

using Clock = std::chrono::steady_clock;

// Interval histogram, in ms: [0,10) [10,23) [23,40) [40,inf). At 60 Hz a
// steady stream lands entirely in the second bucket; ~33 ms intervals (the
// third bucket) mean every other frame was lost.
struct Histogram {
  std::array<uint32_t, 4> buckets{};
  Clock::time_point last{};

  void Add(Clock::time_point now) {
    if (last != Clock::time_point{}) {
      const double ms = std::chrono::duration<double, std::milli>(now - last).count();
      const int i = ms < 10.0 ? 0 : ms < 23.0 ? 1 : ms < 40.0 ? 2 : 3;
      ++buckets[i];
    }
    last = now;
  }
  std::string Take() {
    std::string s = fmt::format("{}/{}/{}/{}", buckets[0], buckets[1], buckets[2], buckets[3]);
    buckets = {};
    return s;
  }
};

struct PresentStats {
  std::mutex mutex;
  uint32_t refreshes = 0;   // images written into the mailbox
  uint32_t paints = 0;      // paints that consumed the mailbox
  uint32_t guest_thread_paints = 0;  // of those, painted by the GPU command thread itself
  uint32_t new_frames = 0;  // paints that found a newer image than the last paint
  uint32_t last_index = UINT32_MAX;
  Histogram refresh_intervals;
  Histogram new_frame_intervals;
};

// With --host_present_vsync_from_guest_output_thread (patched SDK, or MAILBOX), the
// GPU command thread paints inside the SDK's RefreshGuestOutput, i.e. before the
// refresh hook below has counted that frame. The paint notification for pacing is
// deferred until after the refresh is counted so the two counters never cross.
thread_local bool t_in_refresh = false;
thread_local bool t_deferred_new_paint = false;

PresentStats& Stats() {
  static PresentStats stats;
  return stats;
}

// Lifetime totals for present pacing (never reset), guarded by Pacing().mutex.
struct Pacing {
  std::mutex mutex;
  std::condition_variable cv;
  uint64_t swaps = 0;       // VdSwap calls
  uint64_t refreshes = 0;   // images written into the mailbox
  uint64_t new_paints = 0;  // paints that consumed a new image
  // Resync offsets for frames that were never refreshed / never painted.
  int64_t refresh_offset = 0;
  int64_t paint_offset = 0;
  uint32_t timeouts = 0;  // reported and reset by PresentStatsLine
};

Pacing& Pace() {
  static Pacing pacing;
  return pacing;
}

void NotifyPacing(bool refresh) {
  Pacing& p = Pace();
  {
    std::lock_guard<std::mutex> guard(p.mutex);
    ++(refresh ? p.refreshes : p.new_paints);
  }
  p.cv.notify_all();
}

// Builds an Itanium C++ ABI member function pointer ({address, this-adjust 0})
// for a non-virtual member found in the SDK.
template <typename MemberFn>
MemberFn ResolveSdkMember(const char* mangled) {
  void* address = dlsym(RTLD_NEXT, mangled);
  if (!address) {
    std::fprintf(stderr, "present_hooks: cannot resolve %s in the SDK: %s\n", mangled, dlerror());
    std::abort();
  }
  struct {
    void* ptr;
    std::ptrdiff_t adj;
  } rep{address, 0};
  static_assert(sizeof(MemberFn) == sizeof(rep));
  MemberFn fn;
  std::memcpy(&fn, &rep, sizeof(fn));
  return fn;
}

}  // namespace

std::string PresentStatsLine() {
  PresentStats& s = Stats();
  std::lock_guard<std::mutex> guard(s.mutex);
  std::string line = fmt::format(
      "host: {} refreshed, {} painted ({} by GPU thread), {} new ({} dropped); new-frame "
      "intervals <10/10-23/23-40/40+ ms: {}; refresh intervals: {}",
      s.refreshes, s.paints, s.guest_thread_paints, s.new_frames,
      s.refreshes > s.new_frames ? s.refreshes - s.new_frames : 0, s.new_frame_intervals.Take(),
      s.refresh_intervals.Take());
  s.refreshes = s.paints = s.guest_thread_paints = s.new_frames = 0;
  // Blocking time in the SDK's Vulkan paint path; only exported by the patched SDK
  // (patches/rexglue-sdk/0002, scripts/build_sdk.sh), looked up at runtime.
  using TakeWaitStats = void (*)(uint64_t*);
  static const auto take_wait_stats = reinterpret_cast<TakeWaitStats>(
      dlsym(RTLD_DEFAULT, "rex_presenter_take_wait_stats"));
  if (take_wait_stats) {
    uint64_t w[4];
    take_wait_stats(w);
    line += fmt::format("; paint waits ({} paints): submission {:.1f} ms, acquire {:.1f} ms, "
                        "present {:.1f} ms",
                        w[0], w[1] / 1e6, w[2] / 1e6, w[3] / 1e6);
  }
  if (REXCVAR_GET(present_pacing)) {
    Pacing& p = Pace();
    std::lock_guard<std::mutex> pace_guard(p.mutex);
    line += fmt::format("; pacing timeouts {}", p.timeouts);
    p.timeouts = 0;
  }
  return line;
}

void PacePresent() {
  static const bool enabled = REXCVAR_GET(present_pacing);
  if (!enabled) return;
  Pacing& p = Pace();
  std::unique_lock<std::mutex> lock(p.mutex);
  ++p.swaps;
  // Frames before this swap that are not yet in the mailbox / not yet painted.
  auto pending_refresh = [&] {
    return int64_t(p.swaps - 1) - int64_t(p.refreshes) - p.refresh_offset;
  };
  auto pending_paint = [&] {
    return int64_t(p.refreshes) - int64_t(p.new_paints) - p.paint_offset;
  };
  // Counts can only be negative if an earlier resync overshot; undo that part.
  if (pending_refresh() < 0) p.refresh_offset += pending_refresh();
  if (pending_paint() < 0) p.paint_offset += pending_paint();
  static const int64_t allowed = std::max(1, REXCVAR_GET(present_pacing_frames)) - 1;
  const bool done = p.cv.wait_for(lock, std::chrono::milliseconds(50), [&] {
    return pending_refresh() + pending_paint() <= allowed;
  });
  if (!done) {
    // Absorb frames that will never be refreshed/painted, keeping `allowed` in flight.
    ++p.timeouts;
    int64_t excess = pending_refresh() + pending_paint() - allowed;
    const int64_t from_paint = std::min(excess, std::max<int64_t>(0, pending_paint()));
    p.paint_offset += from_paint;
    p.refresh_offset += excess - from_paint;
  }
}

namespace rex::ui {

bool Presenter::RefreshGuestOutput(uint32_t frontbuffer_width, uint32_t frontbuffer_height,
                                   uint32_t display_aspect_ratio_x,
                                   uint32_t display_aspect_ratio_y,
                                   std::function<bool(GuestOutputRefreshContext& context)> refresher) {
  using Fn = decltype(&Presenter::RefreshGuestOutput);
  static const Fn sdk_impl = ResolveSdkMember<Fn>(
      "_ZN3rex2ui9Presenter18RefreshGuestOutputEjjjjSt8functionIFbRNS1_"
      "25GuestOutputRefreshContextEEE");
  t_in_refresh = true;
  const bool refreshed = (this->*sdk_impl)(frontbuffer_width, frontbuffer_height,
                                           display_aspect_ratio_x, display_aspect_ratio_y,
                                           std::move(refresher));
  t_in_refresh = false;
  if (refreshed) {
    PresentStats& s = Stats();
    std::lock_guard<std::mutex> guard(s.mutex);
    ++s.refreshes;
    s.refresh_intervals.Add(Clock::now());
  }
  if (refreshed) NotifyPacing(true);
  if (t_deferred_new_paint) {
    t_deferred_new_paint = false;
    NotifyPacing(false);
  }
  return refreshed;
}

std::unique_lock<std::mutex> Presenter::ConsumeGuestOutput(
    uint32_t& mailbox_index_or_max_if_inactive_out, GuestOutputProperties* properties_out,
    GuestOutputPaintConfig* paint_config_out) {
  using Fn = decltype(&Presenter::ConsumeGuestOutput);
  static const Fn sdk_impl = ResolveSdkMember<Fn>(
      "_ZN3rex2ui9Presenter18ConsumeGuestOutputERjPNS1_21GuestOutputPropertiesEPNS1_"
      "22GuestOutputPaintConfigE");
  std::unique_lock<std::mutex> lock = (this->*sdk_impl)(mailbox_index_or_max_if_inactive_out,
                                                        properties_out, paint_config_out);
  // Only the paint path asks for the paint config; screenshots pass nullptr.
  if (paint_config_out) {
    const uint32_t index = mailbox_index_or_max_if_inactive_out;
    bool is_new = false;
    {
      PresentStats& s = Stats();
      std::lock_guard<std::mutex> guard(s.mutex);
      ++s.paints;
      if (t_in_refresh) ++s.guest_thread_paints;
      // The producer never writes into the acquired image, so a different
      // index means a newer image.
      if (index != UINT32_MAX && index != s.last_index) {
        ++s.new_frames;
        s.new_frame_intervals.Add(Clock::now());
        is_new = true;
      }
      s.last_index = index;
    }
    if (is_new) {
      if (t_in_refresh) {
        t_deferred_new_paint = true;
      } else {
        NotifyPacing(false);
      }
    }
  }
  return lock;
}

}  // namespace rex::ui
