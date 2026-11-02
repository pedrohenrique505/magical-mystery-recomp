// beatles - ReXGlue Recompiled Project
//
// Network imports the SDK gets wrong in ways this game notices.
//
// 1. Overlapped (asynchronous) UDP receive, modeled as "no packet ever arrives".
//
// Why: the game's network layer (guest sub_8293EAA8 / sub_8293ED70) issues an
// overlapped WSARecvFrom, treats SOCKET_ERROR + WSA_IO_PENDING (997) as
// "in flight", and later polls WSAGetOverlappedResult. In the prebuilt SDK
// v0.10.0, WSARecvFrom returns SOCKET_ERROR without setting the last error and
// WSAGetOverlappedResult is a bare stub that leaves r3 untouched (non-zero, so
// the game reads "completed" with garbage). The result was a busy loop that
// re-issued receives forever, flooding the log with ~14 MB/s of STUB warnings
// as soon as a party was created.
//
// How: both imports are defined in the executable (they take precedence over
// the SDK's). WSARecvFrom reports a pending operation; WSAGetOverlappedResult
// always reports it as still incomplete (WSA_IO_INCOMPLETE, 996). The game then
// waits on the overlapped event, which is never signaled. Replace this with a
// real socket implementation if online/system-link play is ever wanted.
//
// 2. XNetRandom returning real random bytes instead of a constant (see below).

#include <sys/random.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/xthread.h>

namespace {

constexpr uint32_t kSocketError = 0xFFFFFFFFu;  // SOCKET_ERROR (-1)
constexpr uint32_t kWsaIoPending = 997;         // WSA_IO_PENDING
constexpr uint32_t kWsaIoIncomplete = 996;      // WSA_IO_INCOMPLETE

}  // namespace

// int WSARecvFrom(caller, socket, buffers, count, bytes_recvd*, flags*, from*,
//                 from_len*, overlapped*, completion_routine) -> r3
REX_EXTERN(__imp__NetDll_WSARecvFrom) {
  (void)base;
  rex::system::XThread::SetLastError(kWsaIoPending);
  ctx.r3.u64 = kSocketError;
}

// BOOL WSAGetOverlappedResult(caller, socket, overlapped*, bytes_transferred*,
//                             wait, flags*) -> r3
REX_EXTERN(__imp__NetDll_WSAGetOverlappedResult) {
  (void)base;
  rex::system::XThread::SetLastError(kWsaIoIncomplete);
  ctx.r3.u64 = 0;  // FALSE
}

// int XNetRandom(caller, BYTE* buffer, UINT length) -> r3
//
// The SDK fills the buffer with a constant 0xBB ("for now, constant values").
// The game builds every BandUser GUID from 16 bytes of XNetRandom (guest
// sub_82517470, called from the BandUser constructor sub_82261328), so all
// users, local and the three pre-allocated remote ones, got the same GUID.
// BandUserMgr::FindSlot (sub_8225A550) matches users to band slots by GUID, so
// the empty remote users looked like band members; SongSyncPanel then waited
// forever for their song lists and the song list never appeared (KI-12).
REX_EXTERN(__imp__NetDll_XNetRandom) {
  const uint32_t buffer = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  if (buffer != 0 && length != 0) {
    uint8_t* out = base + buffer;
    size_t done = 0;
    while (done < length) {
      ssize_t n = getrandom(out + done, length - done, 0);
      if (n <= 0) {
        // Extremely unlikely; fall back to a non-constant source rather than zeros.
        static std::atomic<uint64_t> counter{0x9E3779B97F4A7C15ull};
        uint64_t v = counter.fetch_add(0x9E3779B97F4A7C15ull) ^ uint64_t(done);
        out[done++] = uint8_t(v >> 32);
        continue;
      }
      done += size_t(n);
    }
  }
  ctx.r3.u64 = 0;
}
