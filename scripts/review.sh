#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/artifacts/demo}"
RELOAD_DIR="$(mktemp -d)"
trap 'rm -rf "$RELOAD_DIR"' EXIT

cd "$ROOT"
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure

./build/rtsync demo --out "$OUT"
./build/rtsync reload --artifact "$OUT" --out "$RELOAD_DIR" \
  > "$RELOAD_DIR/reload_report.json"
./build/rtsync verify --artifact "$OUT" \
  > "$RELOAD_DIR/verify_report.json"

jq -e '.status == "pass"' "$RELOAD_DIR/verify_report.json" >/dev/null
jq -e '.status == "pass" and
       .byte_identical == true and
       .fields_and_order_preserved == true' \
  "$RELOAD_DIR/reload_report.json" >/dev/null
jq -e '([.findings[].type] | index("fence_wait_before_signal")) != null and
       ([.findings[].type] | index("cross_device_resource_race")) != null and
       .visual_diffs[0].changed_pixels > 0 and
       (.correlations | length) > 0' "$OUT/analysis.json" >/dev/null

printf 'review passed\n'
jq -r '"  status: " + .status.code +
       "\n  findings: " + (.findings | length | tostring) +
       "\n  correlations: " + (.correlations | length | tostring) +
       "\n  changed pixels: " + (.visual_diffs[0].changed_pixels | tostring)' \
  "$OUT/analysis.json"
jq -r '"  evidence head: " + .evidence_head_sha256' \
  "$RELOAD_DIR/verify_report.json"

