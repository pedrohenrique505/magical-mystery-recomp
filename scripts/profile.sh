#!/usr/bin/env bash
# Runs the game under `perf record` (user-space samples, all threads) for profiling.
# Usage: scripts/profile.sh [output.data] [extra game args]
# Default output: build/perf.data. Also enables --perf_log and --hitch_log_ms=35 (guest
# fps, GPU command counters and per-thread CPU time of slow frames, in build/run-profile.log;
# no --show_fps, which changes how frames are painted, KI-14), and samples the host GPU load into build/gpu_busy.log (one "<unix time> <percent>"
# line every 0.5 s, amdgpu only). Analyze with e.g.
#   perf report -i build/perf.data --no-children --sort comm,dso,sym
# perf_event_paranoid=2 only allows user-space sampling of our own child processes,
# which is why the game is launched by perf instead of attaching with -p.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/perf.data}"
[ $# -gt 0 ] && shift
export RUN_LOG="${RUN_LOG:-$ROOT/build/run-profile.log}"
: > "$RUN_LOG"

BUSY=$(ls /sys/class/drm/card*/device/gpu_busy_percent 2>/dev/null | head -1 || true)
if [ -n "$BUSY" ]; then
  ( while sleep 0.5; do echo "$(date +%s.%N) $(cat "$BUSY")"; done ) > "$ROOT/build/gpu_busy.log" &
  SAMPLER=$!
  trap 'kill $SAMPLER 2>/dev/null || true' EXIT
fi

perf record -F 499 -e cycles:u -o "$OUT" -- "$ROOT/scripts/run.sh" --perf_log=true --hitch_log_ms=35 "$@"
