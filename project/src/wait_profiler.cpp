// beatles - ReXGlue Recompiled Project
//
// Blocking-call profiler (--wait_profile true).
//
// `perf record -e cycles:u` only sees threads while they run; a guest thread
// that is slow because it keeps waiting (timers, events, file reads) is
// invisible there. This wraps the kernel imports the game uses to block or read
// files, measures the host wall time spent inside each call per thread, and
// every 2 s logs the threads with the most blocked time, e.g.
//   wait: XThread953BD6C0 KeDelayExecutionThread 120x 1650.2ms (req 1200.0ms)
// "req" is the sum of requested delays (relative intervals only), so the
// difference to the measured time is oversleep.
//
// Each wrapper forwards to the SDK implementation found with dlsym(RTLD_NEXT).

#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(wait_profile, false, "Debug",
                    "Every 2 s, log per-thread time spent in kernel waits and file reads");

bool HitchTracerEnabled();
void HitchRecordWait(const std::string& thread, const char* import,
                     std::chrono::steady_clock::time_point start,
                     std::chrono::steady_clock::time_point end, uint64_t bytes);

namespace {

using GuestImport = void (*)(PPCContext&, uint8_t*);
using Clock = std::chrono::steady_clock;

struct Stat {
  uint64_t calls = 0;
  double ms = 0.0;
  double requested_ms = 0.0;  // KeDelayExecutionThread only
  uint64_t bytes = 0;         // NtReadFile only
};

struct Profiler {
  std::mutex mutex;
  Clock::time_point window_start = Clock::now();
  // (thread name, import) -> stats
  std::map<std::pair<std::string, const char*>, Stat> stats;
};

Profiler& Prof() {
  static Profiler p;
  return p;
}

const std::string& ThreadName() {
  thread_local std::string name = [] {
    char buf[32] = {};
    pthread_getname_np(pthread_self(), buf, sizeof(buf));
    return std::string(buf) + "/" + std::to_string(syscall(SYS_gettid));
  }();
  return name;
}

void Flush(Profiler& p, Clock::time_point now) {
  const double window_s = std::chrono::duration<double>(now - p.window_start).count();
  std::vector<std::pair<std::pair<std::string, const char*>, Stat>> rows(p.stats.begin(),
                                                                          p.stats.end());
  std::sort(rows.begin(), rows.end(),
            [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
  REXLOG_INFO("wait: --- window {:.2f} s ---", window_s);
  size_t shown = 0;
  for (const auto& [key, s] : rows) {
    // File reads are always listed; waits only for the 12 most blocked threads.
    const bool is_read = s.bytes != 0;
    if (!is_read && shown++ >= 12) continue;
    std::string extra;
    if (s.requested_ms > 0) extra += fmt::format(" (req {:.1f}ms)", s.requested_ms);
    if (s.bytes) extra += fmt::format(" ({:.1f} KB)", s.bytes / 1024.0);
    REXLOG_INFO("wait: {} {} {}x {:.1f}ms{}", key.first, key.second, s.calls, s.ms, extra);
  }
  p.stats.clear();
  p.window_start = now;
}

bool ProfileEnabled() {
  static const bool enabled = REXCVAR_GET(wait_profile);
  return enabled;
}

void Record(const char* import, Clock::time_point start, double ms, double requested_ms,
            uint64_t bytes) {
  if (HitchTracerEnabled()) {
    HitchRecordWait(ThreadName(), import, start,
                    start + std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double, std::milli>(ms)),
                    bytes);
  }
  if (!ProfileEnabled()) return;
  Profiler& p = Prof();
  const Clock::time_point now = Clock::now();
  std::lock_guard<std::mutex> guard(p.mutex);
  Stat& s = p.stats[{ThreadName(), import}];
  ++s.calls;
  s.ms += ms;
  s.requested_ms += requested_ms;
  s.bytes += bytes;
  if (now - p.window_start >= std::chrono::seconds(2)) Flush(p, now);
}

GuestImport Resolve(const char* name) {
  auto fn = reinterpret_cast<GuestImport>(dlsym(RTLD_NEXT, name));
  if (!fn) {
    std::fprintf(stderr, "wait_profiler: cannot resolve %s in the SDK: %s\n", name, dlerror());
    std::abort();
  }
  return fn;
}

// The wrappers also feed the hitch tracer (hitch_tracer.cpp).
bool Enabled() {
  static const bool enabled = ProfileEnabled() || HitchTracerEnabled();
  return enabled;
}

uint64_t LoadBe64(const uint8_t* base, uint32_t addr) {
  uint64_t v;
  std::memcpy(&v, base + addr, sizeof(v));
  return __builtin_bswap64(v);
}

}  // namespace

#define BEATLES_TIMED_IMPORT(name, extra_args)                         \
  REX_EXTERN(__imp__##name) {                                          \
    static const GuestImport sdk_impl = Resolve("__imp__" #name);      \
    if (!Enabled()) {                                                  \
      sdk_impl(ctx, base);                                             \
      return;                                                          \
    }                                                                  \
    double requested_ms = 0.0;                                         \
    uint64_t bytes = 0;                                                \
    extra_args;                                                        \
    const Clock::time_point start = Clock::now();                      \
    sdk_impl(ctx, base);                                               \
    const double ms =                                                  \
        std::chrono::duration<double, std::milli>(Clock::now() - start).count(); \
    Record(#name, start, ms, requested_ms, bytes);                            \
  }

// KeDelayExecutionThread(mode r3, alertable r4, LARGE_INTEGER* interval r5);
// negative intervals are relative, in 100 ns units.
BEATLES_TIMED_IMPORT(KeDelayExecutionThread, {
  if (ctx.r5.u32) {
    const int64_t interval = static_cast<int64_t>(LoadBe64(base, ctx.r5.u32));
    if (interval < 0) requested_ms = -interval / 10000.0;
  }
})
// NtReadFile(handle r3, event r4, apc r5, apc_ctx r6, iosb r7, buffer r8, length r9, offset r10)
BEATLES_TIMED_IMPORT(NtReadFile, { bytes = ctx.r9.u32; })
BEATLES_TIMED_IMPORT(NtReadFileScatter, { bytes = ctx.r9.u32; })
BEATLES_TIMED_IMPORT(KeWaitForSingleObject, {})
BEATLES_TIMED_IMPORT(KeWaitForMultipleObjects, {})
BEATLES_TIMED_IMPORT(NtWaitForSingleObjectEx, {})
BEATLES_TIMED_IMPORT(NtWaitForMultipleObjectsEx, {})
