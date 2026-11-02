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

for p in "$ROOT"/patches/rexglue-sdk/*.patch; do
  [ -e "$p" ] || continue
  if git -C "$SDK" apply --reverse --check "$p" 2>/dev/null; then
    echo "build_sdk.sh: already applied: $(basename "$p")"
  else
    git -C "$SDK" apply "$p"
    echo "build_sdk.sh: applied: $(basename "$p")"
  fi
done

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
