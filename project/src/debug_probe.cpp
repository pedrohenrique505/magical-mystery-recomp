// beatles - ReXGlue Recompiled Project
//
// --debug_probe: a host thread that periodically logs guest state the
// investigation currently cares about. It only reads guest memory; it never
// calls guest code. Addresses come from reverse engineering (see
// progression/known-issues.md) and are specific to this XEX (title 454108B1,
// version 0.0.0.1).
//
// Current watch list: why the song list stays empty after creating a party.
// SongSyncPanel::IsLoaded (guest sub_822E64E0) returns true only when
//   TheContentMgr->state (+40) == 1           (sub_823D3888)
//   panel+74 ("synced") is set by CheckForAdvance (sub_822E6390), which needs
//   the pending-user vector panel+76..+80 to be empty.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(debug_probe, false, "Debug",
                    "Log selected guest state every 2 s (song list investigation)");

namespace {

std::atomic<uint8_t*> g_guest_base{nullptr};

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Guest pointers in the 0x8xxxxxxx image / 0x4xxxxxxx-0x7xxxxxxx heap ranges.
bool Plausible(uint32_t guest) {
  return guest >= 0x40000000u && guest < 0x90000000u;
}

constexpr uint32_t kTheContentMgrPtr = 0x829FEAB8;    // ContentMgr*
constexpr uint32_t kTheSongMgrPtr = 0x829F9798;       // SongMgr*
constexpr uint32_t kSongSyncPanelInstance = 0x82A44F80;  // SongSyncPanel::sInstance
// sub_822D3E00 ("in a networked session?"): obj = *(*(0x82A44AC8) + 84), then a
// virtual call through a virtual-base adjustment, slot +84.
constexpr uint32_t kSessionOwnerPtr = 0x82A44AC8;
// TheNet-like object passed to the send (sub_8238C970 / sub_8238B488): transport
// at +164, player vector at +36/+40, each player's machine id at +60.
constexpr uint32_t kNetPtr = 0x82A47110;
// TheBandUserMgr: remote users at +64/+68. sub_8225AF78 keeps a remote user for
// filter 8 when its virtual slot 0 (via the base at vtbl[+8]) returns true.
constexpr uint32_t kBandUserMgrPtr = 0x82A42284;

void ProbeLoop() {
  uint8_t* base = nullptr;
  while (!(base = g_guest_base.load())) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  auto rd = [base](uint32_t guest) { return Be32(base + guest); };
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    uint32_t cm = rd(kTheContentMgrPtr);
    uint32_t sm = rd(kTheSongMgrPtr);
    uint32_t panel = rd(kSongSyncPanelInstance);
    uint32_t cm_state = Plausible(cm) ? rd(cm + 40) : 0xDEAD;
    uint32_t owner = rd(kSessionOwnerPtr);
    uint32_t sess = Plausible(owner) ? rd(owner + 84) : 0;
    uint32_t sess_fn = 0;
    if (Plausible(sess)) {
      uint32_t vbptr = rd(sess + 4);
      uint32_t sub = Plausible(vbptr) ? sess + rd(vbptr + 4) : 0;  // virtual base subobject - 4
      uint32_t vtbl = Plausible(sub) ? rd(sub + 4) : 0;
      sess_fn = Plausible(vtbl) ? rd(vtbl + 84) : 0;
    }
    REXLOG_INFO("probe: session owner={:08X} obj={:08X} is_networked_fn={:08X}", owner, sess,
                sess_fn);
    uint32_t bum = rd(kBandUserMgrPtr);
    if (Plausible(bum)) {
      std::string remotes;
      uint32_t rb = rd(bum + 64), re = rd(bum + 68);
      for (uint32_t it = rb; Plausible(it) && it < re && it < rb + 64; it += 4) {
        uint32_t u = rd(it);
        uint32_t fn = 0;
        if (Plausible(u)) {
          uint32_t vt = rd(u);
          uint32_t sub = Plausible(vt) ? u + rd(vt + 8) : 0;
          uint32_t vt2 = Plausible(sub) ? rd(sub) : 0;
          fn = Plausible(vt2) ? rd(vt2) : 0;
        }
        remotes += fmt::format(" {:08X}(slot0={:08X})", u, fn);
      }
      REXLOG_INFO("probe: BandUserMgr={:08X} remote users:{}", bum, remotes);
      // Band slots: 4 GUIDs of 16 bytes at +76 (BandUserMgr::FindSlot, sub_8225A550).
      std::string slots;
      for (uint32_t i = 0; i < 4; ++i) {
        uint32_t g = bum + 76 + 16 * i;
        slots += fmt::format(" [{:08X}{:08X}..]", rd(g), rd(g + 4));
      }
      REXLOG_INFO("probe: band slot GUIDs:{}", slots);
    }
    uint32_t net = rd(kNetPtr);
    if (Plausible(net)) {
      uint32_t pb = rd(net + 36), pe = rd(net + 40);
      std::string players;
      for (uint32_t it = pb; Plausible(it) && it < pe && it < pb + 64; it += 4) {
        uint32_t pl = rd(it);
        players += fmt::format(" {:08X}(machine={:08X})", pl, Plausible(pl) ? rd(pl + 60) : 0);
      }
      REXLOG_INFO("probe: net={:08X} transport={:08X} players:{}", net, rd(net + 164), players);
    }
    if (Plausible(panel)) {
      const uint8_t* p = base + panel;
      uint32_t ub = Be32(p + 76), ue = Be32(p + 80);
      std::string pending;
      for (uint32_t it = ub; Plausible(it) && it < ue && it < ub + 64; it += 4) {
        pending += fmt::format(" {:08X}", rd(it));
      }
      REXLOG_INFO("probe: pending user entries:{}", pending);
      uint32_t vb = Be32(p + 76), ve = Be32(p + 80);
      REXLOG_INFO(
          "probe: ContentMgr={:08X} state={} SongMgr={:08X} SongSyncPanel={:08X} sent={} f73={} "
          "synced={} pending_users={}",
          cm, cm_state, sm, panel, p[72], p[73], p[74], ve >= vb ? (ve - vb) / 4 : 0xFFFF);
    } else {
      REXLOG_INFO("probe: ContentMgr={:08X} state={} SongMgr={:08X} SongSyncPanel=none", cm,
                  cm_state, sm);
    }
  }
}

}  // namespace

// Called from the input hooks, which run in guest context and know `base`.
void NoteGuestBase(uint8_t* base) {
  if (g_guest_base.load(std::memory_order_relaxed)) {
    return;
  }
  uint8_t* expected = nullptr;
  if (g_guest_base.compare_exchange_strong(expected, base) && REXCVAR_GET(debug_probe)) {
    REXLOG_INFO("probe: enabled");
    std::thread(ProbeLoop).detach();
  }
}

// Guest memory base once any input hook has run (nullptr before that).
uint8_t* GuestBaseOrNull() {
  return g_guest_base.load(std::memory_order_relaxed);
}
