#!/usr/bin/env bash
# Runs the game with RADV's Radeon GPU Profiler tracing and collects the traces.
#
# Usage: scripts/rgp_capture.sh [N] [extra run.sh args]
#   N (default 3): traces requested automatically during sustained slow stretches
#   (--rgp_slow_frames, project/src/gpu_capture.cpp). `touch build/rgp_trigger`
#   at any time grabs the next frame by hand (e.g. a normal 60 fps frame for
#   comparison). Each trace is ~50-100 MB; RADV writes them to /tmp and this
#   script moves them to build/rgp/ when the game exits. Open them with
#   RGP 1.14.1 (build/tools/rgp-1.14.1, the last release that reads GCN/Vega traces;
#   RGP 2.x shows "no events" for this Renoir iGPU).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
N="${1:-3}"
[ $# -gt 0 ] && shift
mkdir -p "$ROOT/build/rgp"
rm -f "$ROOT/build/rgp_trigger"
stamp="$(mktemp)"
trap 'rm -f "$stamp"' EXIT

status=0
# A full gameplay frame overflows RADV's default thread-trace buffer, and RGP then shows
# only the first ~12 ms of it; instruction timing is what fills the buffer fastest.
# Override either variable to change that.
MESA_VK_TRACE=rgp MESA_VK_TRACE_TRIGGER="$ROOT/build/rgp_trigger" \
  RADV_THREAD_TRACE_BUFFER_SIZE="${RADV_THREAD_TRACE_BUFFER_SIZE:-268435456}" \
  RADV_THREAD_TRACE_INSTRUCTION_TIMING="${RADV_THREAD_TRACE_INSTRUCTION_TIMING:-false}" \
  RUN_LOG="${RUN_LOG:-$ROOT/build/run-rgp.log}" \
  "$ROOT/scripts/run.sh" --rgp_slow_frames="$N" --gpu_debug_markers=true "$@" || status=$?

find /tmp -maxdepth 1 -name 'beatles_*.rgp' -user "$(id -u)" -newer "$stamp" \
  -exec mv -t "$ROOT/build/rgp/" {} +
ls -la "$ROOT/build/rgp/"
exit "$status"
