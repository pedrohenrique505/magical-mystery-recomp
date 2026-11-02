// script_bridge.cpp - lets the game's DTA scripts call into the host: {pc_cfg ...}.
//
// The Harmonix engine runs UI logic in DTA scripts. DataArray::Execute (guest
// 0x823A7768, Ghidra Function_823A7768) evaluates a command `{head args...}`: if `head`
// is a symbol it first looks for an object with that name, then for a registered
// DataFunc (std::map<Symbol, DataFunc*> at 0x82A47CF4). When neither exists it falls
// through to 0x823A78A4, where an optional global "unhandled command" callback is
// tried and otherwise a null node is returned.
//
// A [[entrypoint.midasm_hook]] at 0x823A78A4 calls BeatlesScriptCommandHook with
// r27 = DataArray* (the command), r29 = DataNode* (the result slot) and r30 =
// DataNode* (head, already evaluated). Commands whose head is the symbol `pc_cfg`
// are handled here: the hook writes the result node and returns true, and the
// generated code jumps to the function's epilogue (0x823A795C, which returns r29).
// Every other unknown command keeps the original behaviour. The scripts that use
// `pc_cfg` (main menu "Quit to Desktop", "PC Settings") are added to the game's
// archive by scripts/build_game_overlay.py.
//
// Guest layouts (big-endian; the old Milo DataArray, as in RB2):
//   DataNode  { u32 value; u32 type; }   type 0 int, 1 float, 2 var (value =
//             DataNode*), 5 symbol (value = char*), 0x12 string (value = DataArray*
//             whose first word is the char*).
//   DataArray { DataNode* nodes; Symbol file; s16 size; s16 refs; s16 line; ... }

#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "script_bridge.h"

// debug_probe.cpp: guest memory base, known once an input hook has run.
uint8_t* GuestBaseOrNull();

namespace {

constexpr uint32_t kTypeInt = 0;
constexpr uint32_t kTypeFloat = 1;
constexpr uint32_t kTypeVar = 2;
constexpr uint32_t kTypeSymbol = 5;
constexpr uint32_t kTypeString = 0x12;

// Guest addresses we dereference must lie in the allocated heaps/image.
bool PlausibleGuestPtr(uint32_t addr) {
  return addr >= 0x40000000u && addr < 0xA0000000u;
}

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  return std::byteswap(*reinterpret_cast<const uint32_t*>(base + addr));
}

int16_t Load16(const uint8_t* base, uint32_t addr) {
  return static_cast<int16_t>(std::byteswap(*reinterpret_cast<const uint16_t*>(base + addr)));
}

void Store32(uint8_t* base, uint32_t addr, uint32_t v) {
  *reinterpret_cast<uint32_t*>(base + addr) = std::byteswap(v);
}

std::optional<std::string_view> GuestCString(const uint8_t* base, uint32_t addr) {
  if (!PlausibleGuestPtr(addr)) {
    return std::nullopt;
  }
  const char* s = reinterpret_cast<const char*>(base + addr);
  size_t n = strnlen(s, 256);
  if (n == 256) {
    return std::nullopt;
  }
  return std::string_view(s, n);
}

// Reads a DataNode at `node_addr`, following variables to their value.
std::optional<ScriptValue> ReadNode(const uint8_t* base, uint32_t node_addr) {
  for (int depth = 0; depth < 4; ++depth) {
    if (!PlausibleGuestPtr(node_addr)) {
      return std::nullopt;
    }
    uint32_t value = Load32(base, node_addr);
    uint32_t type = Load32(base, node_addr + 4);
    switch (type) {
      case kTypeInt:
        return ScriptValue{ScriptValue::kInt, static_cast<int32_t>(value), 0.f, {}};
      case kTypeFloat: {
        float f;
        std::memcpy(&f, &value, sizeof(f));
        return ScriptValue{ScriptValue::kFloat, static_cast<int32_t>(f), f, {}};
      }
      case kTypeSymbol: {
        auto s = GuestCString(base, value);
        if (!s) {
          return std::nullopt;
        }
        return ScriptValue{ScriptValue::kSymbol, 0, 0.f, std::string(*s)};
      }
      case kTypeString: {
        if (!PlausibleGuestPtr(value)) {
          return std::nullopt;
        }
        auto s = GuestCString(base, Load32(base, value));
        if (!s) {
          return std::nullopt;
        }
        return ScriptValue{ScriptValue::kString, 0, 0.f, std::string(*s)};
      }
      case kTypeVar:
        node_addr = value;  // the variable's current DataNode
        continue;
      default:
        return std::nullopt;
    }
  }
  return std::nullopt;
}

std::mutex g_handler_mutex;
ScriptCommandHandler g_handler;

}  // namespace

void SetScriptCommandHandler(ScriptCommandHandler handler) {
  std::lock_guard lock(g_handler_mutex);
  g_handler = std::move(handler);
}

// Midasm hook at 0x823A78A4 (DataArray::Execute, unknown command path).
bool BeatlesScriptCommandHook(PPCRegister& r27, PPCRegister& r29, PPCRegister& r30) {
  uint8_t* base = GuestBaseOrNull();
  if (!base) {
    return false;
  }
  const uint32_t array = r27.u32;
  const uint32_t out = r29.u32;
  const uint32_t head = r30.u32;
  if (!PlausibleGuestPtr(array) || !PlausibleGuestPtr(out) || !PlausibleGuestPtr(head)) {
    return false;
  }
  if (Load32(base, head + 4) != kTypeSymbol) {
    return false;
  }
  auto head_name = GuestCString(base, Load32(base, head));
  if (!head_name || *head_name != "pc_cfg") {
    return false;
  }

  const uint32_t nodes = Load32(base, array);
  const int16_t size = Load16(base, array + 8);
  std::vector<ScriptValue> args;
  bool args_ok = PlausibleGuestPtr(nodes) && size >= 1 && size <= 16;
  for (int i = 1; args_ok && i < size; ++i) {
    auto v = ReadNode(base, nodes + 8 * i);
    if (!v) {
      REXLOG_WARN("pc_cfg: argument {} is not a literal, symbol, string or variable", i);
      args_ok = false;
      break;
    }
    args.push_back(std::move(*v));
  }

  int32_t result = 0;
  if (args_ok && !args.empty()) {
    std::lock_guard lock(g_handler_mutex);
    if (g_handler) {
      result = g_handler(args);
    }
  } else if (args_ok) {
    REXLOG_WARN("pc_cfg: called without a subcommand");
  }
  Store32(base, out, static_cast<uint32_t>(result));
  Store32(base, out + 4, kTypeInt);
  return true;
}
