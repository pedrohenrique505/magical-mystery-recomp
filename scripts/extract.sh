#!/usr/bin/env bash
# Extrai a ISO do jogo para ./game (requer extract-xiso no PATH)
set -euo pipefail
ISO="${1:-$HOME/Downloads/Beatles, The - Rock Band (World) (En,Fr,De,Es,It)/Beatles, The - Rock Band (World) (En,Fr,De,Es,It).iso}"
OUT="$(cd "$(dirname "$0")/.." && pwd)/game"
mkdir -p "$OUT"
extract-xiso -d "$OUT" -x "$ISO"
ls -la "$OUT" | head
