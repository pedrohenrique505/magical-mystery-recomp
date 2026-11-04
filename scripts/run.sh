#!/usr/bin/env bash
# Runs the recompiled game. Usage: scripts/run.sh [extra args]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Remove guest-memory shm objects leaked by earlier runs (RAM-backed, 4.8 GB sparse each;
# a full /dev/shm makes the game and the compositor die with SIGBUS). The app unlinks its
# own object at startup, so only leftovers from crashes before that point or older builds
# end up here. Objects still open by a running process are left alone.
for f in /dev/shm/xenia_memory_*; do
  [ -O "$f" ] || continue
  if ! fuser -s "$f" 2>/dev/null; then
    rm -f -- "$f" && echo "run.sh: removed stale $f" >&2
  fi
done

# Make SDL pull audio one guest frame (256 samples, 5.3 ms) at a time. With the default
# device buffer SDL pulls ~4 frames at once, so the SDK runs the game's XAudio callback in
# bursts; the game's mixer (on another guest thread) cannot keep up with back-to-back
# callbacks and resubmits a stale buffer, which sounds like constant crackling (KI-13).
export SDL_AUDIO_DEVICE_SAMPLE_FRAMES="${SDL_AUDIO_DEVICE_SAMPLE_FRAMES:-256}"

# Frame pacing / GPU defaults (KI-14), as REX_<CVAR> environment variables so that any of
# them can still be overridden on the command line (the CLI outranks the environment):
# - FIFO present mode (vsync): IMMEDIATE tears and lets the compositor drop frames.
# - present_pacing: VdSwap waits until the previous frame was painted, locking the guest
#   to the display refresh; without it FIFO drops/repeats frames as the SDK's free-running
#   60 Hz vblank timer drifts against the display, and halves to 30 fps when the GPU is late.
# - Render-target path: host render targets ("fbo", the SDK default). The FSI path
#   (--render_target_path_vulkan=fsi) avoids the EDRAM ownership-transfer draws but does
#   depth/blending in the pixel shader without early-Z, which tripled the main scene pass
#   in the heavy song scenes (RGP, 2026-11-02); fbo is faster there (KI-14).
export REX_VULKAN_ALLOW_PRESENT_MODE_IMMEDIATE="${REX_VULKAN_ALLOW_PRESENT_MODE_IMMEDIATE:-false}"
export REX_VULKAN_ALLOW_PRESENT_MODE_MAILBOX="${REX_VULKAN_ALLOW_PRESENT_MODE_MAILBOX:-false}"
export REX_VULKAN_ALLOW_PRESENT_MODE_FIFO_RELAXED="${REX_VULKAN_ALLOW_PRESENT_MODE_FIFO_RELAXED:-false}"
export REX_PRESENT_PACING="${REX_PRESENT_PACING:-true}"
export REX_RENDER_TARGET_PATH_VULKAN="${REX_RENDER_TARGET_PATH_VULKAN:-fbo}"
# Upstream Xenia's default (ReXGlue ships false): run the vertex shader of unclipped draws
# (clears, full-screen quads) on the CPU to bound the EDRAM range they touch. Without it, the
# whole EDRAM can be taken as used and host render-target ownership transfers copy up to 2048
# tiles each; in "Sgt. Pepper" this halved transfer time and removed nearly all frames over
# 16.7 ms of GPU work (KI-14).
export REX_EXECUTE_UNCLIPPED_DRAW_VS_ON_CPU="${REX_EXECUTE_UNCLIPPED_DRAW_VS_ON_CPU:-true}"

# Keyboard emulates a guitar alongside any connected gamepad. The game has five
# frets; the Brazilian ABNT2 Ç key activates overdrive. Arrow keys navigate menus and Space strums.
# Environment variables and command-line cvars can override these defaults.
export REX_MNK_MODE="${REX_MNK_MODE:-true}"
export REX_KEYBIND_A="${REX_KEYBIND_A:-G,Return}"
export REX_KEYBIND_B="${REX_KEYBIND_B:-H,Escape}"
export REX_KEYBIND_Y="${REX_KEYBIND_Y:-J}"
export REX_KEYBIND_X="${REX_KEYBIND_X:-K}"
export REX_KEYBIND_LEFT_SHOULDER="${REX_KEYBIND_LEFT_SHOULDER:-L}"
export REX_KEYBIND_BACK="${REX_KEYBIND_BACK:-Semicolon,Tab}"
export REX_KEYBIND_START="${REX_KEYBIND_START:-P}"
export REX_KEYBIND_DPAD_UP="${REX_KEYBIND_DPAD_UP:-Space,Up}"
export REX_KEYBIND_DPAD_DOWN="${REX_KEYBIND_DPAD_DOWN:-Down}"
export REX_KEYBIND_DPAD_LEFT="${REX_KEYBIND_DPAD_LEFT:-Left}"
export REX_KEYBIND_DPAD_RIGHT="${REX_KEYBIND_DPAD_RIGHT:-Right}"
export REX_KEYBIND_LSTICK_UP="${REX_KEYBIND_LSTICK_UP:-Up}"
export REX_KEYBIND_LSTICK_DOWN="${REX_KEYBIND_LSTICK_DOWN:-Down}"
export REX_KEYBIND_LSTICK_LEFT="${REX_KEYBIND_LSTICK_LEFT:-Left}"
export REX_KEYBIND_LSTICK_RIGHT="${REX_KEYBIND_LSTICK_RIGHT:-Right}"
# Clear SDK defaults that overlap with guitar keys.
export REX_KEYBIND_RIGHT_TRIGGER="${REX_KEYBIND_RIGHT_TRIGGER-}"
export REX_KEYBIND_RSTICK_PRESS="${REX_KEYBIND_RSTICK_PRESS-}"

# Runtime libraries rebuilt from source with patches/rexglue-sdk (scripts/build_sdk.sh)
# take precedence over the prebuilt SDK when present.
SDK_LIBS="$ROOT/tools/rexglue/linux-amd64/lib"
if [ -f "$ROOT/build/sdk-lib/librexruntime.so" ]; then
  SDK_LIBS="$ROOT/build/sdk-lib:$SDK_LIBS"
  echo "run.sh: using patched SDK runtime from build/sdk-lib" >&2
fi
if [ -f "$ROOT/build/sdk-lib/librexgpu-xenos.so" ]; then
  export REXGLUE_GPU_PLUGIN_DIR="$ROOT/build/sdk-lib"
  echo "run.sh: using patched GPU plugin from build/sdk-lib" >&2
fi
export LD_LIBRARY_PATH="$SDK_LIBS:${LD_LIBRARY_PATH:-}"

# Game data: build/game_overlay is game/ plus our script changes (in-game "Quit to Desktop"
# and PC settings menu, project/mods/), rebuilt here whenever its inputs change. The original
# dump in game/ is never modified. GAME_DATA_ROOT=game scripts/run.sh runs the stock scripts.
if [ -z "${GAME_DATA_ROOT:-}" ]; then
  python3 -I "$ROOT/scripts/build_game_overlay.py" --if-stale
  GAME_DATA_ROOT="$ROOT/build/game_overlay"
fi

# Log file: RUN_LOG=build/foo.log scripts/run.sh keeps A/B runs apart (the SDK appends).
RUN_LOG="${RUN_LOG:-$ROOT/build/run.log}"
exec "$ROOT/build/beatles/beatles" --game_data_root "$GAME_DATA_ROOT" --gpu_plugin xenos --log_file "$RUN_LOG" "$@"
