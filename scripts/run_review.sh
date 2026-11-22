#!/usr/bin/env bash
# End-to-end reviewable pipeline for the reflection probe:
#   record (v1 + v2) -> truncate -> recover -> replay -> review.
# Produces artifacts/traces/* and artifacts/evidence/*.
set -euo pipefail
cd "$(dirname "$0")/.."

export LD_LIBRARY_PATH="$PWD/third_party/prefix/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
PROBE=./build/probe
TR=artifacts/traces
EV=artifacts/evidence
mkdir -p "$TR" "$EV"

if [ ! -x "$PROBE" ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
fi

echo "== 1. record reference captures (legacy v1 + current v2) =="
$PROBE record --format v2 --out "$TR/full_v2.rprb" --png "$EV/reference.png"
$PROBE record --format v1 --out "$TR/full_v1.rprb"

echo "== 2. simulate a crashed capture: truncated v2 record =="
$PROBE truncate --in "$TR/full_v2.rprb" --out "$TR/truncated_v2.rprb" \
  --keep-records 7 --partial-bytes 11

echo "== 3. recover the truncated capture =="
$PROBE recover --in "$TR/truncated_v2.rprb" --out "$TR/recovered_v2.rprb"

echo "== 4. replay the recovered capture =="
$PROBE replay --trace "$TR/recovered_v2.rprb" --out-dir "$EV"

echo "== 5. review: reference vs recovered replay =="
set +e
$PROBE review --reference "$TR/full_v2.rprb" --candidate "$TR/truncated_v2.rprb" \
  --out-dir "$EV"
rc=$?
set -e

echo "== 6. legacy compatibility: v1 vs v1 =="
mkdir -p /tmp/probe_evidence_v1
$PROBE review --reference "$TR/full_v1.rprb" --candidate "$TR/full_v1.rprb" \
  --out-dir /tmp/probe_evidence_v1

echo "review exit code: $rc (2 = first error found, see $EV/report.json)"
exit 0
