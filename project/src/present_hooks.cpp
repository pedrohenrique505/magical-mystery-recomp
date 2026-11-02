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

#include <dlfcn.h>

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

#include <fmt/format.h>

#include <rex/ui/presenter.h>

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
  uint32_t paints = 0;      // UI thread paints that consumed the mailbox
  uint32_t new_frames = 0;  // paints that found a newer image than the last paint
  uint32_t last_index = UINT32_MAX;
  Histogram refresh_intervals;
  Histogram new_frame_intervals;
};

PresentStats& Stats() {
  static PresentStats stats;
  return stats;
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
      "host: {} refreshed, {} painted, {} new ({} dropped); new-frame intervals "
      "<10/10-23/23-40/40+ ms: {}; refresh intervals: {}",
      s.refreshes, s.paints, s.new_frames,
      s.refreshes > s.new_frames ? s.refreshes - s.new_frames : 0, s.new_frame_intervals.Take(),
      s.refresh_intervals.Take());
  s.refreshes = s.paints = s.new_frames = 0;
  return line;
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
  const bool refreshed = (this->*sdk_impl)(frontbuffer_width, frontbuffer_height,
                                           display_aspect_ratio_x, display_aspect_ratio_y,
                                           std::move(refresher));
  if (refreshed) {
    PresentStats& s = Stats();
    std::lock_guard<std::mutex> guard(s.mutex);
    ++s.refreshes;
    s.refresh_intervals.Add(Clock::now());
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
    PresentStats& s = Stats();
    std::lock_guard<std::mutex> guard(s.mutex);
    ++s.paints;
    // The producer never writes into the acquired image, so a different index
    // means a newer image.
    if (index != UINT32_MAX && index != s.last_index) {
      ++s.new_frames;
      s.new_frame_intervals.Add(Clock::now());
    }
    s.last_index = index;
  }
  return lock;
}

}  // namespace rex::ui
