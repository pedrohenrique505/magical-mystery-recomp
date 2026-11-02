#!/usr/bin/env bash
# Builds the ReXGlue SDK runtime libraries from source with this project's patches.
#
# The prebuilt SDK (tools/rexglue/linux-amd64) stays the source of headers, CMake config,
# the codegen binary and static libraries; librexruntime.so and the xenos GPU plugin
# (librexgpu-xenos.so, which holds the Vulkan backend) are rebuilt. scripts/run.sh puts
# build/sdk-lib first in LD_LIBRARY_PATH and points REXGLUE_GPU_PLUGIN_DIR (patch 0003) at
# it when the files exist, so the game picks them up without relinking (same v0.10.0
# source; the executable needs no symbol that the rebuilt libraries lack).
#
# Usage: scripts/build_sdk.sh            # apply patches, build, copy libs to build/sdk-lib
#        scripts/build_sdk.sh --remove   # delete build/sdk-lib (back to the prebuilt SDK)
#
# Patches live in patches/rexglue-sdk/*.patch and are applied to third_party/rexglue-sdk
# (a v0.10.0 checkout) if not applied yet. RAM: -j2 peaks well under 2 GB.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/rexglue-sdk"
BUILD="$ROOT/build/sdk"
OUT="$ROOT/build/sdk-lib"

if [ "${1:-}" = "--remove" ]; then
  rm -rf -- "$OUT"
  echo "build_sdk.sh: removed $OUT; scripts/run.sh uses the prebuilt SDK again"
  exit 0
fi

# Rebuild the expected content of every patched file in a temporary copy (HEAD + all patches,
# in order) and copy over only the files that differ, so the step is idempotent and leaves
# unchanged files' timestamps alone (no needless recompiles). Per-patch "already applied"
# checks do not work once a later patch edits lines next to an earlier one.
PATCHES=("$ROOT"/patches/rexglue-sdk/*.patch)
if [ -e "${PATCHES[0]}" ]; then
  STAGE="$(mktemp -d "$ROOT/build/sdk-patch.XXXXXX")"
  trap 'rm -rf -- "$STAGE"' EXIT
  mapfile -t PATCHED < <(sed -n 's|^+++ w/||p' "${PATCHES[@]}" | sort -u)
  git -C "$SDK" archive HEAD "${PATCHED[@]}" | tar -x -C "$STAGE"
  for p in "${PATCHES[@]}"; do
    patch -s -p1 -d "$STAGE" < "$p"
  done
  for f in "${PATCHED[@]}"; do
    if ! cmp -s "$STAGE/$f" "$SDK/$f"; then
      cp -f -- "$STAGE/$f" "$SDK/$f"
      echo "build_sdk.sh: updated $f"
    fi
  done
  echo "build_sdk.sh: ${#PATCHES[@]} patches in place"
fi

if [ ! -f "$BUILD/build.ninja" ]; then
  cmake -S "$SDK" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_STANDARD=23 \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=mold
fi

nice -n 10 ninja -C "$BUILD" -j2 rexruntime rexgpu-xenos

mkdir -p "$OUT"
for lib in librexruntime.so librexgpu-xenos.so; do
  cp -f -- "$SDK/out/linux-amd64/$lib" "$OUT/$lib"
  echo "build_sdk.sh: $OUT/$lib"
done
