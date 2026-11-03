// pc_settings.cpp - host side of the in-game "PC Settings" screen and "Quit to Desktop".
//
// The screen and menu entries are DTA scripts added by scripts/build_game_overlay.py
// (project/mods/); they call {pc_cfg <subcommand> ...}, which script_bridge.cpp
// forwards here. Each setting maps onto an SDK or project cvar; the player's choices
// are stored in <user_data_root>/pc_settings.toml and applied at startup, below the
// command line and REX_* environment variables (so those still win for testing).
//
// Subcommands (all return an int to the script):
//   {pc_cfg get <key>}       current value
//   {pc_cfg toggle <key>}    flip a boolean setting; returns the new value
//   {pc_cfg cycle <key>}     next value of a multi-choice setting; returns it
//   {pc_cfg pending <key>}   1 if the stored value only takes effect after a restart
//   {pc_cfg quit}            close the game (same path as closing the window)
//   {pc_cfg cvar <name>}     value of any int/bool cvar (bools: 0/1), for script-side
//                            knobs such as the venue detail cvars below
//
// Keys:
//   full_rate_world   venue mode (world_rate_hooks.cpp, venue_detail.dta)       live
//                     0 = 30 FPS (the engine's own rate), 1 = 60 FPS (full rate while
//                     it fits, adaptive), 2 = 60 FPS Performance (same, plus crowd
//                     impostor LOD 1 and no depth of field: ~2 ms less GPU per frame)
//   fullscreen        bool  SDK window fullscreen                               live
//   letterbox         bool  SDK present_letterbox (keep 16:9, else stretch)     live
//   resolution_scale  1, 4, 2, 3 (cycle order): internal render scale        restart
//                     1x, 4 = "2x Horizontal" (draw_resolution_scale_x 2, y 1), 2x, 3x;
//                     scaled choices also keep small offscreen surfaces at 1x
//                     (draw_resolution_scale_threshold 640, MSAA mask 5 - patch 0010,
//                     the sharp rule measured in KI-17)
//   texture_filter    1,2,4,8,16  SDK anisotropic_override (1 = off)            live
//   antialiasing      0 off, 1 FXAA, 2 FXAA extreme: SDK swap_post_effect       live
//                     (patch 0008 makes it switch without a restart)

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "pc_settings.h"
#include "script_bridge.h"

// Venue detail knobs read by the venue scripts (project/mods/venue_detail.dta) through
// {pc_cfg cvar ...} at every camera shot. Defaults keep the game's own values.
REXCVAR_DEFINE_INT32(venue_crowd_pct, 100, "Beatles",
                     "Venue flat (impostor) crowd density in percent of the camera shot's own (0-100)");
REXCVAR_DEFINE_INT32(venue_crowd_3d_pct, 100, "Beatles",
                     "Venue 3D crowd characters in percent of the camera shot's own (0-100)");
REXCVAR_DEFINE_INT32(venue_crowd_lod, 0, "Beatles",
                     "Lowest crowd impostor texture LOD (0 = 256x512, 1 = 128x256, 2 = 64x128)");
REXCVAR_DEFINE_BOOL(venue_no_dof, false, "Beatles", "Turn off the camera shots' depth of field");
REXCVAR_DEFINE_BOOL(venue_no_self_shadow, false, "Beatles",
                    "Turn off the characters' self-shadow maps");

namespace {

struct Setting {
  const char* key;
  const char* cvar;
  // Values the setting cycles through, as shown to the script (bools: 0/1).
  std::vector<int32_t> choices;
  bool needs_restart;
  // Script value <-> cvar string.
  std::function<std::string(int32_t)> to_cvar;
  std::function<int32_t(const std::string&)> from_cvar;
  // Optional: every cvar the value sets (settings that span several cvars); when empty,
  // just {cvar, to_cvar(value)}.
  std::function<std::vector<std::pair<std::string, std::string>>(int32_t)> to_cvars;
};

std::string BoolStr(int32_t v) { return v ? "true" : "false"; }
int32_t ParseBool(const std::string& s) { return s == "true" || s == "1"; }
int32_t ParseInt(const std::string& s) {
  try {
    return std::stoi(s);
  } catch (...) {
    return 0;
  }
}

// anisotropic_override takes the Xenos AnisoFilter enum: 0 off, 1 1x, 2 2x, 3 4x, 4 8x,
// 5 16x (-1 = whatever the game asks for). The menu shows the multiplier.
std::string AnisoToCvar(int32_t mult) {
  switch (mult) {
    case 2: return "2";
    case 4: return "3";
    case 8: return "4";
    case 16: return "5";
    default: return "0";
  }
}
int32_t AnisoFromCvar(const std::string& s) {
  switch (ParseInt(s)) {
    case 2: return 2;
    case 3: return 4;
    case 4: return 8;
    case 5: return 16;
    default: return 1;
  }
}

// Internal resolution choices (see the header comment).
constexpr int32_t kResolution2xHorizontal = 4;
std::vector<std::pair<std::string, std::string>> ResolutionToCvars(int32_t v) {
  bool horizontal = v == kResolution2xHorizontal;
  int32_t scale = horizontal ? 1 : v;
  bool scaled = horizontal || v > 1;
  return {
      {"resolution_scale", std::to_string(scale)},
      {"draw_resolution_scale_x", horizontal ? "2" : std::to_string(scale)},
      {"draw_resolution_scale_y", std::to_string(scale)},
      {"draw_resolution_scale_threshold", scaled ? "640" : "0"},
      {"draw_resolution_scale_threshold_msaa_mask", "5"},
  };
}
int32_t ResolutionFromCvars(const std::string& resolution_scale) {
  int32_t scale = ParseInt(resolution_scale);
  if (scale <= 1 && ParseInt(rex::cvar::GetFlagByName("draw_resolution_scale_x")) == 2 &&
      ParseInt(rex::cvar::GetFlagByName("draw_resolution_scale_y")) <= 1) {
    return kResolution2xHorizontal;
  }
  return std::max(scale, int32_t(1));
}

// Venue mode (see the key list above). Performance = the cheapest settings that kept the
// full-rate venue at ~60 fps on a Vega 6 with acceptable looks (log.md, 2026-11-02).
std::vector<std::pair<std::string, std::string>> VenueToCvars(int32_t v) {
  return {
      {"full_rate_world", v > 0 ? "true" : "false"},
      {"full_rate_world_adaptive", "true"},
      {"venue_crowd_lod", v == 2 ? "1" : "0"},
      {"venue_no_dof", v == 2 ? "true" : "false"},
  };
}
int32_t VenueFromCvars(const std::string& full_rate_world) {
  if (!ParseBool(full_rate_world)) return 0;
  return ParseInt(rex::cvar::GetFlagByName("venue_crowd_lod")) > 0 ||
                 ParseBool(rex::cvar::GetFlagByName("venue_no_dof"))
             ? 2
             : 1;
}

std::string AaToCvar(int32_t v) {
  return v == 1 ? "fxaa" : v == 2 ? "fxaa_extreme" : "none";
}
int32_t AaFromCvar(const std::string& s) {
  return s == "fxaa" ? 1 : s == "fxaa_extreme" ? 2 : 0;
}

const std::array<Setting, 6>& Settings() {
  static const std::array<Setting, 6> settings = {{
      {"full_rate_world", "full_rate_world", {0, 1, 2}, false, BoolStr, VenueFromCvars,
       VenueToCvars},
      {"fullscreen", "fullscreen", {0, 1}, false, BoolStr, ParseBool},
      {"letterbox", "present_letterbox", {0, 1}, false, BoolStr, ParseBool},
      {"resolution_scale", "resolution_scale", {1, kResolution2xHorizontal, 2, 3}, true,
       [](int32_t v) { return std::to_string(v == kResolution2xHorizontal ? 1 : v); },
       ResolutionFromCvars, ResolutionToCvars},
      {"texture_filter", "anisotropic_override", {1, 2, 4, 8, 16}, false, AnisoToCvar,
       AnisoFromCvar},
      {"antialiasing", "swap_post_effect", {0, 1, 2}, false, AaToCvar, AaFromCvar},
  }};
  return settings;
}

const Setting* FindSetting(std::string_view key) {
  for (const auto& s : Settings()) {
    if (key == s.key) {
      return &s;
    }
  }
  return nullptr;
}

std::mutex g_mutex;
std::filesystem::path g_settings_path;
std::function<void()> g_request_quit;
std::function<void(std::function<void()>)> g_run_on_ui_thread;
std::map<std::string, int32_t> g_stored;         // what the player chose (pc_settings.toml)
std::map<std::string, int32_t> g_active_at_boot;  // restart-only settings as launched

int32_t CurrentValue(const Setting& s) {
  auto it = g_stored.find(s.key);
  if (it != g_stored.end()) {
    return it->second;
  }
  return s.from_cvar(rex::cvar::GetFlagByName(s.cvar));
}

void Save() {
  std::error_code ec;
  std::filesystem::create_directories(g_settings_path.parent_path(), ec);
  std::ofstream out(g_settings_path);
  if (!out) {
    REXLOG_ERROR("pc_settings: cannot write {}", g_settings_path.string());
    return;
  }
  out << "# In-game PC Settings (Options > PC Settings). Written by the game.\n";
  for (const auto& s : Settings()) {
    auto it = g_stored.find(s.key);
    if (it != g_stored.end()) {
      out << s.key << " = " << it->second << "\n";
    }
  }
}

void Apply(const Setting& s, int32_t value) {
  g_stored[s.key] = value;
  Save();
  if (s.needs_restart) {
    REXLOG_INFO("pc_settings: {} = {} (applies after a restart)", s.key, value);
    return;
  }
  auto cvars = s.to_cvars ? s.to_cvars(value)
                          : std::vector<std::pair<std::string, std::string>>{
                                {s.cvar, s.to_cvar(value)}};
  for (const auto& [cvar, str] : cvars) {
    REXLOG_INFO("pc_settings: {} = {} ({} = {})", s.key, value, cvar, str);
  }
  // Change callbacks (e.g. fullscreen) touch the window: run them on the UI thread.
  auto set = [cvars] {
    for (const auto& [cvar, str] : cvars) {
      if (!rex::cvar::SetFlagByName(cvar, str)) {
        REXLOG_WARN("pc_settings: {} rejected value {}", cvar, str);
      }
    }
  };
  if (g_run_on_ui_thread) {
    g_run_on_ui_thread(set);
  } else {
    set();
  }
}

int32_t HandleCommand(const std::vector<ScriptValue>& args) {
  const ScriptValue& cmd = args[0];
  if (cmd.kind != ScriptValue::kSymbol) {
    REXLOG_WARN("pc_cfg: subcommand must be a symbol");
    return 0;
  }
  if (cmd.s == "quit") {
    REXLOG_INFO("pc_cfg: quit to desktop requested from the game menu");
    if (g_request_quit) {
      g_request_quit();
    }
    return 1;
  }
  if (cmd.s == "cvar") {
    if (args.size() < 2 || args[1].kind != ScriptValue::kSymbol) {
      REXLOG_WARN("pc_cfg cvar: expected a cvar name");
      return 0;
    }
    std::string v = rex::cvar::GetFlagByName(args[1].s);
    return v == "true" ? 1 : v == "false" ? 0 : ParseInt(v);
  }
  if (args.size() < 2 || args[1].kind != ScriptValue::kSymbol) {
    REXLOG_WARN("pc_cfg {}: expected a setting name", cmd.s);
    return 0;
  }
  const Setting* s = FindSetting(args[1].s);
  if (!s) {
    REXLOG_WARN("pc_cfg {}: unknown setting '{}'", cmd.s, args[1].s);
    return 0;
  }
  std::lock_guard lock(g_mutex);
  int32_t current = CurrentValue(*s);
  if (cmd.s == "get") {
    return current;
  }
  if (cmd.s == "pending") {
    auto it = g_active_at_boot.find(s->key);
    return it != g_active_at_boot.end() && it->second != current;
  }
  if (cmd.s == "toggle" || cmd.s == "cycle") {
    const auto& c = s->choices;
    auto it = std::find(c.begin(), c.end(), current);
    int32_t next = (it == c.end() || it + 1 == c.end()) ? c.front() : *(it + 1);
    Apply(*s, next);
    return next;
  }
  REXLOG_WARN("pc_cfg: unknown subcommand '{}'", cmd.s);
  return 0;
}

// Loads pc_settings.toml ("key = int" lines).
void Load() {
  std::ifstream in(g_settings_path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    auto trim = [](std::string v) {
      v.erase(0, v.find_first_not_of(" \t"));
      v.erase(v.find_last_not_of(" \t\r") + 1);
      return v;
    };
    std::string key = trim(line.substr(0, eq));
    const Setting* s = FindSetting(key);
    if (!s) {
      REXLOG_WARN("pc_settings: unknown key '{}' in {}", key, g_settings_path.string());
      continue;
    }
    int32_t v = ParseInt(trim(line.substr(eq + 1)));
    if (std::find(s->choices.begin(), s->choices.end(), v) == s->choices.end()) {
      REXLOG_WARN("pc_settings: invalid value {} for {}", v, key);
      continue;
    }
    g_stored[key] = v;
  }
}

}  // namespace

void LoadPcSettings(const std::filesystem::path& user_data_root) {
  std::lock_guard lock(g_mutex);
  g_settings_path = user_data_root / "pc_settings.toml";
  Load();
  // Apply the choices as a config file rather than with SetFlagByName: several cvars
  // (resolution_scale, anisotropic_override) belong to the GPU plugin, which registers
  // them only later, and the SDK keeps config values for not-yet-registered cvars
  // until they appear (SetFlagByName just fails). Config also ranks below the REX_*
  // environment and the command line, which therefore still win.
  if (!g_stored.empty()) {
    auto applied = g_settings_path;
    applied.replace_filename("pc_settings.cvars.toml");
    {
      std::ofstream out(applied);
      out << "# Generated from pc_settings.toml at startup; do not edit.\n";
      for (const auto& s : Settings()) {
        auto it = g_stored.find(s.key);
        if (it != g_stored.end()) {
          auto cvars = s.to_cvars ? s.to_cvars(it->second)
                                  : std::vector<std::pair<std::string, std::string>>{
                                        {s.cvar, s.to_cvar(it->second)}};
          for (const auto& [name, v] : cvars) {
            bool is_number_or_bool = v == "true" || v == "false" ||
                                     v.find_first_not_of("-0123456789") == std::string::npos;
            out << name << " = " << (is_number_or_bool ? v : "\"" + v + "\"") << "\n";
          }
        }
      }
    }
    rex::cvar::LoadConfig(applied);
  }
  for (const auto& s : Settings()) {
    if (s.needs_restart) {
      g_active_at_boot[s.key] = CurrentValue(s);
    }
  }
  if (!g_stored.empty()) {
    REXLOG_INFO("pc_settings: applied {} setting(s) from {}", g_stored.size(),
                g_settings_path.string());
  }
}

void InitPcSettings(std::function<void()> request_quit,
                    std::function<void(std::function<void()>)> run_on_ui_thread) {
  g_request_quit = std::move(request_quit);
  g_run_on_ui_thread = std::move(run_on_ui_thread);
  SetScriptCommandHandler(&HandleCommand);
}
