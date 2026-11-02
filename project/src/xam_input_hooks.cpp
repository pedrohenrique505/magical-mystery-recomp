// beatles - ReXGlue Recompiled Project
//
// Overrides for the XAM input imports the game uses. These definitions live in
// the executable, so the generated code binds to them instead of the SDK's
// shared library; each one forwards to the SDK implementation, looked up with
// dlsym(RTLD_NEXT).
//
// 1. Serialization (KI-7). rex::input::InputSystem (prebuilt SDK v0.10.0) has
//    no locking, yet every GetState/GetCapabilities/SetState call runs
//    RefreshDevices(), which clears and rebuilds shared vectors. The game polls
//    XInputGetState from its own thread (guest sub_823E4D20, every 4 ms) while
//    other threads call XInputGetCapabilities/XInputSetState, which corrupted
//    the heap (random intro freezes and "free(): invalid pointer" aborts). All
//    three imports now share one mutex. Drop this once the SDK locks itself.
//
// 2. Instrument emulation (--instruments). The game decides what a controller
//    is from XINPUT_CAPABILITIES.SubType (guest sub_823E5028): 6 guitar,
//    11 bass, 8 drum kit; anything else is a plain gamepad, which the game only
//    accepts as a vocalist (on the console the headset plugs into the pad), so
//    it asks for a microphone. Overriding SubType makes a regular pad show up
//    as the chosen instrument. Standard Rock Band button layout applies:
//    guitar/bass frets A/B/Y/X/LB (green/red/yellow/blue/orange), strum on
//    D-pad up/down, overdrive on Back; drums pads A/B/Y/X, kick on LB.

#include <dlfcn.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_STRING(instruments, "guitar", "Input",
                      "Instrument each controller emulates, comma-separated per player "
                      "(gamepad, guitar, bass, drums). E.g. \"guitar,drums\": player 1 "
                      "guitar, player 2 drums; missing entries repeat the last one. \"gamepad\" keeps the "
                      "real subtype (vocalist only, needs a microphone)");

namespace {

using GuestImport = void (*)(PPCContext&, uint8_t*);

constexpr uint32_t kMaxUsers = 4;
constexpr uint32_t kXUserIndexAny = 0xFF;
constexpr uint8_t kSubTypeKeep = 0;  // leave whatever the SDK reported

std::mutex& InputMutex() {
  static std::mutex mutex;
  return mutex;
}

GuestImport ResolveSdkImport(const char* name) {
  auto fn = reinterpret_cast<GuestImport>(dlsym(RTLD_NEXT, name));
  if (!fn) {
    std::fprintf(stderr, "xam_input_hooks: cannot resolve %s in the SDK: %s\n", name, dlerror());
    std::abort();
  }
  return fn;
}

uint8_t SubTypeFor(std::string_view name) {
  if (name == "guitar") return 0x06;  // XINPUT_DEVSUBTYPE_GUITAR
  if (name == "bass") return 0x0B;    // XINPUT_DEVSUBTYPE_GUITAR_BASS
  if (name == "drums") return 0x08;   // XINPUT_DEVSUBTYPE_DRUM_KIT
  if (name != "gamepad" && !name.empty()) {
    REXLOG_WARN("--instruments: unknown instrument '{}', using gamepad", name);
  }
  return kSubTypeKeep;
}

// Parsed once; the cvar is read at the first capabilities query.
const std::array<uint8_t, kMaxUsers>& InstrumentSubTypes() {
  static const std::array<uint8_t, kMaxUsers> table = [] {
    std::array<uint8_t, kMaxUsers> t{};
    std::string spec = REXCVAR_GET(instruments);
    std::string_view rest = spec;
    uint8_t last = kSubTypeKeep;
    for (uint32_t i = 0; i < kMaxUsers; ++i) {
      if (!rest.empty()) {
        size_t comma = rest.find(',');
        std::string_view item = rest.substr(0, comma);
        while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
        while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
        last = SubTypeFor(item);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
      }
      t[i] = last;
    }
    REXLOG_INFO("Instrument emulation (--instruments={}): subtypes {:#x} {:#x} {:#x} {:#x}", spec,
                t[0], t[1], t[2], t[3]);
    return t;
  }();
  return table;
}

}  // namespace

void NoteGuestBase(uint8_t* base);  // debug_probe.cpp

void LogInstrumentConfig() {
  InstrumentSubTypes();
}

#define BEATLES_SERIALIZED_IMPORT(name)                          \
  REX_EXTERN(name) {                                             \
    static const GuestImport sdk_impl = ResolveSdkImport(#name); \
    NoteGuestBase(base);                                         \
    std::lock_guard<std::mutex> guard(InputMutex());             \
    sdk_impl(ctx, base);                                         \
  }

BEATLES_SERIALIZED_IMPORT(__imp__XamInputGetState)
BEATLES_SERIALIZED_IMPORT(__imp__XamInputSetState)

// XamInputGetCapabilities(user_index r3, flags r4, X_INPUT_CAPABILITIES* r5) -> r3
REX_EXTERN(__imp__XamInputGetCapabilities) {
  static const GuestImport sdk_impl = ResolveSdkImport("__imp__XamInputGetCapabilities");
  const uint32_t user_index = ctx.r3.u32;
  const uint32_t caps_ptr = ctx.r5.u32;
  const auto& sub_types = InstrumentSubTypes();  // parse (and log) on the first query
  {
    std::lock_guard<std::mutex> guard(InputMutex());
    sdk_impl(ctx, base);
  }
  if (ctx.r3.u32 != 0 || caps_ptr == 0) {
    return;  // device not connected or bad pointer: nothing to rewrite
  }
  uint32_t slot = user_index == kXUserIndexAny ? 0 : user_index;
  if (slot >= kMaxUsers) {
    return;
  }
  uint8_t sub_type = sub_types[slot];
  if (sub_type != kSubTypeKeep) {
    base[caps_ptr + 1] = sub_type;  // X_INPUT_CAPABILITIES::sub_type (byte, no swap)
  }
}
