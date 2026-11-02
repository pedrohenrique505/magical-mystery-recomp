// pc_settings.h - host side of the in-game PC settings menu (see pc_settings.cpp).

#pragma once

#include <filesystem>
#include <functional>

// Reads <user_data_root>/pc_settings.toml and applies it to the cvars. Call before
// the runtime and GPU are created (OnConfigurePaths).
void LoadPcSettings(const std::filesystem::path& user_data_root);

// Installs the {pc_cfg ...} command handler. `request_quit` closes the app;
// `run_on_ui_thread` runs a function on the UI thread. Both may be called from any thread.
void InitPcSettings(std::function<void()> request_quit,
                    std::function<void(std::function<void()>)> run_on_ui_thread);
