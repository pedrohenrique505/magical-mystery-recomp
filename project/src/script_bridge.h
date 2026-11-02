// script_bridge.h - host side of the {pc_cfg ...} DTA command (see script_bridge.cpp).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// One evaluated argument of a {pc_cfg ...} command.
struct ScriptValue {
  enum Kind { kInt, kFloat, kSymbol, kString } kind;
  int32_t i;     // kInt (and kFloat truncated)
  float f;       // kFloat
  std::string s; // kSymbol / kString
};

// Receives the arguments after `pc_cfg` (at least one); returns the integer result
// of the command. Runs on the guest thread that executed the script.
using ScriptCommandHandler = std::function<int32_t(const std::vector<ScriptValue>& args)>;

void SetScriptCommandHandler(ScriptCommandHandler handler);
