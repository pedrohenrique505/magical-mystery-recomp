// beatles - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <memory>

#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/debug_overlay.h>

// xam_input_hooks.cpp: parses --instruments and logs the result.
void LogInstrumentConfig();
// shm_cleanup.cpp: unlinks the guest memory shm object so it cannot leak.
void UnlinkGuestMemoryShm();
// perf_hooks.cpp: guest frame-rate counter (VdSwap) and its overlays.
std::unique_ptr<rex::ui::ImGuiDialog> CreateFpsOverlay(rex::ui::ImGuiDrawer* drawer);
rex::ui::FrameStats GuestFrameStats();

class BeatlesApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BeatlesApp>(new BeatlesApp(ctx, "beatles",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  void OnPostSetup() override {
    UnlinkGuestMemoryShm();
    LogInstrumentConfig();
    SetGuestFrameStats(&GuestFrameStats);  // shown by the SDK debug overlay (F3)
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    fps_overlay_ = CreateFpsOverlay(drawer);  // --show_fps
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override { fps_overlay_.reset(); }
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  std::unique_ptr<rex::ui::ImGuiDialog> fps_overlay_;
};
