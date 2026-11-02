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

export LD_LIBRARY_PATH="$ROOT/tools/rexglue/linux-amd64/lib:${LD_LIBRARY_PATH:-}"
exec "$ROOT/build/beatles/beatles" --game_data_root "$ROOT/game" --gpu_plugin xenos --log_file "$ROOT/build/run.log" "$@"
