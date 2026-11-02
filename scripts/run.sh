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

# Log file: RUN_LOG=build/foo.log scripts/run.sh keeps A/B runs apart (the SDK appends).
RUN_LOG="${RUN_LOG:-$ROOT/build/run.log}"
exec "$ROOT/build/beatles/beatles" --game_data_root "$ROOT/game" --gpu_plugin xenos --log_file "$RUN_LOG" "$@"
