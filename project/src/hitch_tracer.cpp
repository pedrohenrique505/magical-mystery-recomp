// beatles - ReXGlue Recompiled Project
//
// Hitch tracer (--hitch_log_ms N, 0 = off).
//
// The aggregate --perf_log histogram says hitches exist; this says what the
// process was doing during each one. On every VdSwap it snapshots the CPU time
// of every thread in the process (per-thread scheduler clocks, one syscall per
// thread). When a swap interval exceeds N ms it logs, for that interval:
//   - the threads that used the most CPU (busy vs blocked: a thread with
//     cpu ~= wall time was running the whole time);
//   - every kernel wait/read (from wait_profiler.cpp's wrappers) longer than
//     1 ms that overlapped the interval, with its offset from the window start;
//   - how long the previous VdSwap call itself took.
// Example:
//   hitch: t=84.213s 61.4 ms (prev swap call 0.1 ms), swap thread 1240 cpu 3.0 ms, ...
//          thread cpu ms: | Main XThread/1234 58.0 | GPU Commands/1200 40.1 ...
//   hitch:   wait XThread0012/1240 KeWaitForSingleObject 2x 55.0ms in window (longest ...)
//   hitch:   read +2.1ms XThread0013/1241 NtReadFile 35.2ms (64.0 KB)
//
// Times are seconds since the first swap, so they can be lined up with
// --perf_log lines and other logs.

#include <dirent.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(hitch_log_ms, 0, "Debug",
                     "Log what every thread was doing during each guest frame interval longer "
                     "than this many ms (0 = off)");

namespace {

using Clock = std::chrono::steady_clock;

struct WaitEvent {
  Clock::time_point start;
  Clock::time_point end;
  std::string thread;
  const char* import;
  uint64_t bytes;
};

struct ThreadInfo {
  std::string name;
  clockid_t clock;
  int64_t last_cpu_ns = -1;  // at the previous swap; -1 = no baseline yet
};

struct Tracer {
  std::mutex mutex;
  std::deque<WaitEvent> waits;  // recent events, pruned by age
  std::unordered_map<pid_t, ThreadInfo> threads;
  uint32_t swaps_since_rescan = 0;
  Clock::time_point origin{};
};

Tracer& T() {
  static Tracer t;
  return t;
}

int32_t Threshold() {
  static const int32_t ms = REXCVAR_GET(hitch_log_ms);
  return ms;
}

// Linux per-thread CPU clock for any thread of this process (see
// MAKE_THREAD_CPUCLOCK in the kernel): CPUCLOCK_SCHED | CPUCLOCK_PERTHREAD_MASK.
clockid_t ThreadClock(pid_t tid) { return static_cast<clockid_t>((~tid << 3) | 4 | 2); }

int64_t CpuNs(clockid_t clock) {
  timespec ts;
  if (clock_gettime(clock, &ts) != 0) return -1;  // thread exited
  return int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

std::string ReadComm(pid_t tid) {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
  FILE* f = std::fopen(path, "r");
  if (!f) return "?";
  char buf[32] = {};
  if (!std::fgets(buf, sizeof(buf), f)) buf[0] = 0;
  std::fclose(f);
  buf[std::strcspn(buf, "\n")] = 0;
  return buf;
}

// Adds threads that appeared and refreshes names (guest threads are renamed
// after they start). Exited threads are dropped when their clock fails.
void Rescan(Tracer& t) {
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
    const pid_t tid = static_cast<pid_t>(std::atoi(e->d_name));
    auto [it, inserted] = t.threads.try_emplace(tid);
    if (inserted) it->second.clock = ThreadClock(tid);
    it->second.name = ReadComm(tid);
  }
  closedir(dir);
}

double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

}  // namespace

bool HitchTracerEnabled() { return Threshold() > 0; }

// Called by wait_profiler.cpp for every timed import.
void HitchRecordWait(const std::string& thread, const char* import, Clock::time_point start,
                     Clock::time_point end, uint64_t bytes) {
  if (end - start < std::chrono::milliseconds(1)) return;
  Tracer& t = T();
  std::lock_guard<std::mutex> guard(t.mutex);
  t.waits.push_back({start, end, thread, import, bytes});
  // Keep ~1 s of history: enough for any interval we would report.
  while (!t.waits.empty() && end - t.waits.front().end > std::chrono::seconds(1)) {
    t.waits.pop_front();
  }
}

// Called by perf_hooks.cpp on every VdSwap, before the SDK call.
// `prev` is the previous swap's time; `prev_call_ms` how long that VdSwap call took.
void HitchOnSwap(Clock::time_point prev, Clock::time_point now, double prev_call_ms) {
  if (!HitchTracerEnabled()) return;
  Tracer& t = T();
  std::lock_guard<std::mutex> guard(t.mutex);
  if (t.origin == Clock::time_point{}) t.origin = now;
  if (t.swaps_since_rescan++ % 60 == 0) Rescan(t);

  const double frame_ms = prev == Clock::time_point{} ? 0.0 : Ms(now - prev);
  const bool hitch = frame_ms > Threshold();

  struct Row {
    double cpu_ms;
    const std::string* name;
    pid_t tid;
  };
  std::vector<Row> rows;
  for (auto it = t.threads.begin(); it != t.threads.end();) {
    const int64_t cpu = CpuNs(it->second.clock);
    if (cpu < 0) {
      it = t.threads.erase(it);
      continue;
    }
    if (hitch && it->second.last_cpu_ns >= 0) {
      rows.push_back({(cpu - it->second.last_cpu_ns) / 1e6, &it->second.name, it->first});
    }
    it->second.last_cpu_ns = cpu;
    ++it;
  }
  if (!hitch) return;

  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.cpu_ms > b.cpu_ms; });
  std::string cpu_list;
  double total_cpu = 0.0;
  for (const Row& r : rows) total_cpu += r.cpu_ms;
  for (size_t i = 0; i < rows.size() && i < 8 && rows[i].cpu_ms >= 1.0; ++i) {
    cpu_list += fmt::format(" | {}/{} {:.1f}", *rows[i].name, rows[i].tid, rows[i].cpu_ms);
  }
  // The thread calling VdSwap is the guest render thread; always show its CPU.
  const pid_t self = static_cast<pid_t>(syscall(SYS_gettid));
  double self_cpu = 0.0;
  for (const Row& r : rows) {
    if (r.tid == self) self_cpu = r.cpu_ms;
  }
  REXLOG_INFO("hitch: t={:.3f}s {:.1f} ms (prev swap call {:.1f} ms), swap thread {} cpu {:.1f} "
              "ms, process cpu {:.1f} ms; thread cpu ms:{}",
              std::chrono::duration<double>(now - t.origin).count(), frame_ms, prev_call_ms,
              self, self_cpu, total_cpu, cpu_list);
  // Waits overlapping the interval, grouped per (thread, import): count, time
  // inside the interval, longest single call, first offset. File reads are
  // listed individually.
  struct Group {
    uint32_t calls = 0;
    double ms = 0.0, max_ms = 0.0, first_ms = 1e9;
  };
  std::vector<std::pair<std::string, Group>> groups;
  for (const WaitEvent& w : t.waits) {
    if (w.end <= prev || w.start >= now) continue;
    const double offset = Ms(w.start - prev);
    const double inside = Ms(std::min(w.end, now) - std::max(w.start, prev));
    const double total = Ms(w.end - w.start);
    if (w.bytes) {
      REXLOG_INFO("hitch:   read {:+.1f}ms {} {} {:.1f}ms ({:.1f} KB)", offset, w.thread,
                  w.import, total, w.bytes / 1024.0);
      continue;
    }
    std::string key = fmt::format("{} {}", w.thread, w.import);
    auto it = std::find_if(groups.begin(), groups.end(),
                           [&](const auto& g) { return g.first == key; });
    if (it == groups.end()) it = groups.insert(groups.end(), {std::move(key), Group{}});
    Group& g = it->second;
    ++g.calls;
    g.ms += inside;
    g.max_ms = std::max(g.max_ms, total);
    g.first_ms = std::min(g.first_ms, offset);
  }
  std::sort(groups.begin(), groups.end(),
            [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
  for (const auto& [key, g] : groups) {
    REXLOG_INFO("hitch:   wait {} {}x {:.1f}ms in window (longest {:.1f}ms, first at {:+.1f}ms)",
                key, g.calls, g.ms, g.max_ms, g.first_ms);
  }
}
