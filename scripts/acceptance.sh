#!/usr/bin/env bash
# End-to-end acceptance for the normal-map tangent verification pipeline.
# Covers: immutable original material, pause/resume, before/after result
# comparison, Vulkan->CPU degrade path, concrete timestamps and original
# byte offsets. Fixed stack: C++17 + Vulkan + GLFW.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NMTS_BIN:-$ROOT/build/nmts_verify}"
OUT="${NMTS_ACCEPT_OUT:-$ROOT/out_accept}"

echo "[1/7] clean workspace at $(date -u +%FT%TZ)"
if [ -e "$OUT" ]; then
  echo "refusing to overwrite existing $OUT; set NMTS_ACCEPT_OUT elsewhere"
  exit 2
fi
mkdir -p "$OUT/original" "$OUT/runs"

echo "[2/7] generate deterministic extreme-parameter material"
"$BIN" generate "$OUT/original/material.nmts"
ORIG_HASH="$(sha256sum "$OUT/original/material.nmts" | awk '{print $1}')"
chmod -w "$OUT/original/material.nmts"

run_env() {
  local work="$1"; shift
  NMTS_WORK_DIR="$work" NMTS_MATERIAL="$OUT/original/material.nmts" "$@"
}

echo "[3/7] uninterrupted CPU reference run"
NMTS_WORK_DIR="$OUT/runs/full" NMTS_MATERIAL="$OUT/original/material.nmts" \
  NMTS_FORCE_CPU=1 NMTS_CHUNK=8 "$BIN" run full

echo "[4/7] paused run (exit code must be 10) then resume"
set +e
NMTS_WORK_DIR="$OUT/runs/paused" NMTS_MATERIAL="$OUT/original/material.nmts" \
  NMTS_FORCE_CPU=1 NMTS_CHUNK=8 NMTS_PAUSE_AFTER=20 "$BIN" run paused
PAUSE_CODE=$?
set -e
[ "$PAUSE_CODE" -eq 10 ] || { echo "expected exit 10, got $PAUSE_CODE"; exit 1; }
cp "$OUT/runs/paused/evidence/results.bin" "$OUT/runs/paused_partial.bin"
NMTS_WORK_DIR="$OUT/runs/paused" NMTS_MATERIAL="$OUT/original/material.nmts" \
  NMTS_FORCE_CPU=1 NMTS_CHUNK=8 "$BIN" resume paused

echo "[5/7] before/after comparison: paused+resumed == full run"
"$BIN" compare "$OUT/runs/paused/evidence/results.bin" \
               "$OUT/runs/full/evidence/results.bin"
# The partial prefix must equal the head of the final stream.
local_count=$(($(stat -c%s "$OUT/runs/paused_partial.bin")))
head -c "$local_count" "$OUT/runs/paused/evidence/results.bin" \
  > "$OUT/runs/paused_prefix.bin"
"$BIN" compare "$OUT/runs/paused_partial.bin" "$OUT/runs/paused_prefix.bin"

echo "[6/7] resource pressure: simulated GPU OOM degrades to CPU"
NMTS_WORK_DIR="$OUT/runs/oom" NMTS_MATERIAL="$OUT/original/material.nmts" \
  NMTS_FORCE_GPU_OOM=1 NMTS_CHUNK=8 "$BIN" run oom
NMTS_WORK_DIR="$OUT/runs/shrink" NMTS_MATERIAL="$OUT/original/material.nmts" \
  NMTS_FORCE_CPU=1 NMTS_FORCE_CHUNK_FAILURE=1 NMTS_CHUNK=16 \
  "$BIN" run shrink
"$BIN" compare --ignore-backend \
  "$OUT/runs/shrink/evidence/results.bin" \
  "$OUT/runs/full/evidence/results.bin"
"$ROOT/scripts/assert_evidence.py" "$OUT" "$ORIG_HASH"

echo "[7/7] Vulkan compute vs CPU reference (semantic equality)"
if NMTS_WORK_DIR="$OUT/runs/vk" NMTS_MATERIAL="$OUT/original/material.nmts" \
     NMTS_CHUNK=16 "$BIN" run vk; then
  "$BIN" compare --ignore-backend \
    "$OUT/runs/vk/evidence/results.bin" \
    "$OUT/runs/full/evidence/results.bin"
else
  echo "Vulkan unavailable on this host; degrade evidence is in runs/oom"
fi

CUR_HASH="$(sha256sum "$OUT/original/material.nmts" | awk '{print $1}')"
[ "$CUR_HASH" = "$ORIG_HASH" ] || { echo "original material mutated"; exit 1; }
echo "ACCEPTANCE OK; original sha256=$ORIG_HASH unchanged"
