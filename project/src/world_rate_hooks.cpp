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

#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(full_rate_world, false, "Beatles",
                    "Render the 3D venue every frame instead of at the engine's emulated "
                    "film rate (RndPostProc emulate_fps; usually every other frame)");

// debug_probe.cpp: guest memory base, known once an input hook has run.
uint8_t* GuestBaseOrNull();

namespace {

constexpr uint64_t kProcessAll = 7;
constexpr uint32_t kPostProcCurrentPtr = 0x82ACC198;  // RndPostProc::sCurrent
constexpr uint32_t kEmulateFpsOffset = 0x158;         // RndPostProc::mEmulateFPS (float)

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

}  // namespace

// Called at the entry of ProcCounter::ProcCommands (0x8241BFC0); r3 = this.
// Returning true makes the guest function return with r3 as its result.
bool BeatlesProcCommandsHook(PPCRegister& r3) {
  LogEmulateFpsChange();
  if (!REXCVAR_GET(full_rate_world)) {
    return false;
  }
  r3.u64 = kProcessAll;
  return true;
}
