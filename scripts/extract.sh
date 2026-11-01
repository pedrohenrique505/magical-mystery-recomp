#!/usr/bin/env bash
# Extracts the game ISO into ./game (requires extract-xiso on PATH). Usage: scripts/extract.sh <path-to-iso>
set -euo pipefail
ISO="${1:-$HOME/Downloads/Beatles, The - Rock Band (World) (En,Fr,De,Es,It)/Beatles, The - Rock Band (World) (En,Fr,De,Es,It).iso}"
OUT="$(cd "$(dirname "$0")/.." && pwd)/game"
mkdir -p "$OUT"
extract-xiso -d "$OUT" -x "$ISO"
ls -la "$OUT" | head
