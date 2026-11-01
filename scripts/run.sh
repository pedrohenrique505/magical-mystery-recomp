#!/usr/bin/env bash
# Executa o build recompilado. Uso: scripts/run.sh [args extras]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$ROOT/tools/rexglue/linux-amd64/lib:${LD_LIBRARY_PATH:-}"
exec "$ROOT/build/beatles/beatles" --game_data_root "$ROOT/game" --gpu_plugin xenos --log_file "$ROOT/build/run.log" "$@"
