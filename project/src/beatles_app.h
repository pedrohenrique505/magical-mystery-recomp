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
  // No achievement toast. Any registered ImGui dialog makes the SDK presenter
  // paint from the UI thread on request; there each paint queues behind the
  // game's GPU work with only 3 submissions in flight, and in gameplay the UI
  // thread managed only ~25-40 paints/s, so half the guest frames were dropped
  // in the presenter mailbox (KI-14). With no UI drawers the GPU command
  // thread presents every guest frame right after producing it. --show_fps
  // and the F3/F4 overlays bring the UI-thread mode back while they are open.
  std::unique_ptr<rex::ui::AchievementNotificationDialog>
  CreateAchievementNotificationDialog() override {
    return nullptr;
  }
  void OnShutdown() override { fps_overlay_.reset(); }
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  std::unique_ptr<rex::ui::ImGuiDialog> fps_overlay_;
};
