#!/usr/bin/env bash
set -euo pipefail

BIN="${1:?binary path required}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
OUT="${WORK}/artifact"

"$BIN" --out "$OUT" --width 64 --height 40 --frames 4 --pause-after 3
python3 "$ROOT/tools/review_artifact.py" "$OUT"
"$BIN" verify "$OUT"
"$BIN" inspect "$OUT"

EXTERNAL_OUT="${WORK}/artifact-resumed-external"
"$BIN" resume --checkpoint "$OUT/paused/checkpoint.json" --out "$EXTERNAL_OUT"
cmp "$OUT/resumed/final.ppm" "$EXTERNAL_OUT/final.ppm"

CPU_OUT="${WORK}/artifact-cpu"
"$BIN" --out "$CPU_OUT" --width 48 --height 32 --frames 3 --pause-after 2 --backend cpu
python3 "$ROOT/tools/review_artifact.py" "$CPU_OUT"
"$BIN" verify "$CPU_OUT"

echo "end-to-end artifact and independent review checks passed"

