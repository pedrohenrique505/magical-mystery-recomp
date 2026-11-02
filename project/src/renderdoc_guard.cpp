// beatles - ReXGlue Recompiled Project
//
// Stop the SDK from injecting RenderDoc into every run.
//
// rex::ui::RenderDocAPI::CreateIfConnected() is meant to detect an attached
// RenderDoc, but it does dlopen("librenderdoc.so") without RTLD_NOLOAD: once
// RenderDoc is installed system-wide, that call *loads* it, so every run gets
// RenderDoc's hooks. On a Wayland window the game then never shows up (nothing
// is painted) and runs at ~18 fps.
//
// The function is exported by librexruntime and reached through the GOT (from
// librexgpu-xenos and from the runtime itself, built with -fno-plt), so this
// definition in the executable preempts it. It only forwards to the SDK when
// librenderdoc.so is already loaded, i.e. the game was launched under RenderDoc
// on purpose (ENABLE_VULKAN_RENDERDOC_CAPTURE=1, see gpu_capture.cpp).

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

#include <rex/ui/renderdoc_api.h>

namespace rex::ui {

std::unique_ptr<RenderDocAPI> RenderDocAPI::CreateIfConnected() {
  void* loaded = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
  if (!loaded) return nullptr;
  dlclose(loaded);  // drop the reference NOLOAD added
  using Fn = std::unique_ptr<RenderDocAPI> (*)();
  static const Fn sdk_impl = [] {
    auto fn = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "_ZN3rex2ui12RenderDocAPI17CreateIfConnectedEv"));
    if (!fn) {
      std::fprintf(stderr, "renderdoc_guard: cannot resolve CreateIfConnected: %s\n", dlerror());
      std::abort();
    }
    return fn;
  }();
  return sdk_impl();
}

}  // namespace rex::ui
