#!/usr/bin/env bash
# Unattended gameplay benchmark: boots the game, drives the menus with a virtual gamepad
# (scripts/virtual_pad.py) into Quickplay > "Sgt. Pepper's Lonely Hearts Club Band" (guitar,
# easy, no-fail), measures for BENCH_SECONDS once gameplay starts, then closes the game.
#
#   scripts/bench_song.sh <label> [extra game args...]
#
# Writes build/bench_<label>.log (SDK log with --perf_log) and build/bench_<label>.csv
# (--gpu_frame_csv); summarise with scripts/bench_summary.py <label>... Run on AC power and do
# not touch the machine meanwhile: the game must stay visible and focused (a hidden window
# loses vsync and skews the numbers). Environment variables for the game (e.g. RADV_DEBUG)
# pass through.
#
# Menu navigation: if build/bench_macro.txt exists (BENCH_MACRO overrides the path), it is
# replayed at its recorded times from the first logged frame: record one with
# scripts/record_pad.py by navigating with your own controller (much faster than the built-in
# sequence). Otherwise a slow built-in sequence is used, which assumes a fresh boot with the
# default sorting.
set -euo pipefail
cd "$(dirname "$0")/.."
label="$1"
shift
BENCH_SECONDS="${BENCH_SECONDS:-60}"
macro="${BENCH_MACRO:-build/bench_macro.txt}"
runtime="${XDG_RUNTIME_DIR:-/tmp}"
fifo="$runtime/beatles_bench_pad"
log="build/bench_${label}.log"
csv="build/bench_${label}.csv"

.venv/bin/python -I scripts/virtual_pad.py --fifo "$fifo" > "build/bench_pad.log" 2>&1 &
pad_pid=$!
game_pid=""
cleanup() {
  [ -n "$game_pid" ] && kill "$game_pid" 2>/dev/null || true
  pkill -x beatles 2>/dev/null || true
  kill "$pad_pid" 2>/dev/null || true
}
trap cleanup EXIT
sleep 1
pad() { for c in "$@"; do echo "$c" > "$fifo"; done; }

: > "$log"
# No ImGui overlay during a measurement (it moves painting to the UI thread, KI-14).
export REX_BIND_DEBUG_OVERLAY="${REX_BIND_DEBUG_OVERLAY:-none}"
RUN_LOG="$log" setsid scripts/run.sh --perf_log=true --gpu_frame_csv="$csv" "$@" \
  > build/bench_game.out 2>&1 &
game_pid=$!

# Wait for the first frames.
for _ in $(seq 1200); do grep -q "world_rate: postproc" "$log" 2>/dev/null && break; sleep 0.05; done
hyprctl dispatch focuswindow 'title:^beatles' > /dev/null 2>&1 || true
if [ -f "$macro" ]; then
  echo "bench_song: replaying $macro"
  cat "$macro" > "$fifo" &
else
sleep 18
pad "press A"; sleep 5            # skip the intro movie
pad "press START"; sleep 5        # title -> main menu
pad "press A"; sleep 4            # Quickplay
pad "press A"; sleep 5            # Solo/Band Quickplay
pad "press A"; sleep 6            # Local Quickplay
pad "press A"; sleep 4            # join
pad "press BACK"; sleep 2         # no-fail mode on
pad "press START"; sleep 5        # song list
pad "press BACK"; sleep 2         # sort alphabetically
for _ in $(seq 12); do pad "press Y"; sleep 0.4; done   # next category x12 -> R
sleep 1
pad "dpad down"; sleep 0.5; pad "dpad down"; sleep 0.5; pad "dpad down"; sleep 1  # Sgt. Pepper
pad "press A"; sleep 4            # choose song
pad "press A"; sleep 3            # guitar
pad "press A"                     # easy
fi

for _ in $(seq 1200); do grep -q "emulate_fps=30.00" "$log" && break; sleep 0.1; done
if ! grep -q "emulate_fps=30.00" "$log"; then
  echo "bench_song: gameplay did not start (menu navigation failed?)" >&2
  grim -s 0.4 -t jpeg -q 60 "build/bench_${label}_fail.jpg" 2>/dev/null || true
  exit 1
fi
echo "bench_song: gameplay started, measuring ${BENCH_SECONDS} s"
# BENCH_SHOTS="20 45": screenshots (build/bench_<label>_<s>s.jpg) that many seconds in.
elapsed=0
for t in ${BENCH_SHOTS:-}; do
  sleep $((t - elapsed)); elapsed=$t
  grim -s 0.5 -t jpeg -q 70 "build/bench_${label}_${t}s.jpg" 2>/dev/null || true
done
sleep $((BENCH_SECONDS - elapsed))
echo "bench_song: done ($log, $csv)"
