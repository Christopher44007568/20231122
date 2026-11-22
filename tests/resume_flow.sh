#!/usr/bin/env bash
set -euo pipefail

binary="${1:?binary path required}"
work="${2:?work directory required}"
rm -rf "$work"
mkdir -p "$work"
evidence="$work/evidence.jsonl"

"$binary" --mode full --out "$work/full.json" \
  --evidence "$evidence" --reset-evidence >/dev/null
"$binary" --mode pause --out "$work/paused.json" \
  --state "$work/checkpoint.vstate" --evidence "$evidence" >/dev/null
"$binary" --mode resume --out "$work/resumed.json" \
  --state "$work/checkpoint.vstate" --evidence "$evidence" >/dev/null
"$binary" --mode compare --a "$work/full.json" --b "$work/resumed.json" \
  --out "$work/compare.json" --evidence "$evidence" >/dev/null

grep -q '"verdict": "identical"' "$work/compare.json"
grep -q '"beforeChanged": false' "$work/compare.json"
grep -q '"afterChanged": false' "$work/compare.json"
grep -q '"event":"state-saved"' "$evidence"
grep -q '"event":"state-loaded"' "$evidence"

"$binary" --mode full --budget-kb 256 --out "$work/full_low.json" \
  --evidence "$evidence" >/dev/null
"$binary" --mode pause --budget-kb 256 --out "$work/paused_low.json" \
  --state "$work/checkpoint_low.vstate" --evidence "$evidence" >/dev/null
"$binary" --mode resume --out "$work/resumed_low.json" \
  --state "$work/checkpoint_low.vstate" --evidence "$evidence" >/dev/null
"$binary" --mode compare --a "$work/full_low.json" --b "$work/resumed_low.json" \
  --out "$work/compare_low.json" --evidence "$evidence" >/dev/null

grep -q '"verdict": "identical"' "$work/compare_low.json"
grep -q 'instance-capacity-clamped' "$work/full_low.json"

"$binary" --mode full --force-cpu --out "$work/full_cpu.json" \
  --evidence "$evidence" >/dev/null
grep -q 'cpu-forced;cpu-fallback' "$work/full_cpu.json"

echo "resume flow passed"
