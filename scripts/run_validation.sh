#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

./scripts/build.sh
ctest --test-dir build --output-on-failure

binary="$root/build/vegdist"
artifacts="$root/artifacts"
degraded="$artifacts/degraded"
cmake -E make_directory "$degraded"
cmake -E rm -f \
  "$artifacts/result_full.json" \
  "$artifacts/result_paused.json" \
  "$artifacts/result_resumed.json" \
  "$artifacts/compare_full_resumed.json" \
  "$artifacts/checkpoint.vstate" \
  "$artifacts/evidence.jsonl" \
  "$artifacts/SHA256SUMS"
cmake -E remove_directory "$degraded"
cmake -E make_directory "$degraded"

"$binary" --mode full --out "$artifacts/result_full.json" \
  --evidence "$artifacts/evidence.jsonl" --reset-evidence
"$binary" --mode pause --out "$artifacts/result_paused.json" \
  --state "$artifacts/checkpoint.vstate" \
  --evidence "$artifacts/evidence.jsonl"
"$binary" --mode resume --out "$artifacts/result_resumed.json" \
  --state "$artifacts/checkpoint.vstate" \
  --evidence "$artifacts/evidence.jsonl"
"$binary" --mode compare --a "$artifacts/result_full.json" \
  --b "$artifacts/result_resumed.json" \
  --out "$artifacts/compare_full_resumed.json" \
  --evidence "$artifacts/evidence.jsonl"

low_evidence="$degraded/evidence.jsonl"
"$binary" --mode full --budget-kb 256 \
  --out "$degraded/result_full_low.json" \
  --evidence "$low_evidence" --reset-evidence
"$binary" --mode pause --budget-kb 256 \
  --out "$degraded/result_paused_low.json" \
  --state "$degraded/checkpoint_low.vstate" --evidence "$low_evidence"
"$binary" --mode resume \
  --out "$degraded/result_resumed_low.json" \
  --state "$degraded/checkpoint_low.vstate" --evidence "$low_evidence"
"$binary" --mode compare --a "$degraded/result_full_low.json" \
  --b "$degraded/result_resumed_low.json" \
  --out "$degraded/compare_low.json" --evidence "$low_evidence"

"$binary" --mode full --force-cpu \
  --out "$degraded/result_full_cpu.json" --evidence "$low_evidence"
"$binary" --mode compare --a "$artifacts/result_full.json" \
  --b "$degraded/result_full_cpu.json" \
  --out "$degraded/compare_vulkan_cpu.json" --evidence "$low_evidence"

VK_ICD_FILENAMES=/nonexistent/icd.json "$binary" --mode full \
  --out "$degraded/result_full_vulkan_unavailable.json" \
  --evidence "$low_evidence"
"$binary" --mode compare --a "$artifacts/result_full.json" \
  --b "$degraded/result_full_vulkan_unavailable.json" \
  --out "$degraded/compare_vulkan_unavailable.json" \
  --evidence "$low_evidence"

expected="$artifacts/compare_full_resumed.json"
grep -q '"verdict": "identical"' "$expected"
grep -q '"beforeChanged": false' "$expected"
grep -q '"afterChanged": false' "$expected"
grep -q '"countDiffs": \[\]' "$expected"
grep -q '"instance-capacity-clamped:192"' "$degraded/result_full_low.json"
grep -q 'cpu-forced;cpu-fallback' "$degraded/result_full_cpu.json"
grep -q 'vulkan-unavailable:' "$degraded/result_full_vulkan_unavailable.json"

(
  cd "$root"
  sha256sum \
    artifacts/result_full.json \
    artifacts/result_paused.json \
    artifacts/result_resumed.json \
    artifacts/compare_full_resumed.json \
    artifacts/checkpoint.vstate \
    artifacts/evidence.jsonl \
    artifacts/degraded/result_full_low.json \
    artifacts/degraded/result_paused_low.json \
    artifacts/degraded/result_resumed_low.json \
    artifacts/degraded/compare_low.json \
    artifacts/degraded/checkpoint_low.vstate \
    artifacts/degraded/result_full_cpu.json \
    artifacts/degraded/compare_vulkan_cpu.json \
    artifacts/degraded/result_full_vulkan_unavailable.json \
    artifacts/degraded/compare_vulkan_unavailable.json \
    artifacts/degraded/evidence.jsonl \
    artifacts/EVIDENCE.md \
    > artifacts/SHA256SUMS
)

echo "validation complete"
echo "normal: $(grep '"bufferHash"' "$artifacts/result_full.json" | head -1)"
echo "low budget: $(grep '"bufferHash"' "$degraded/result_full_low.json" | head -1)"
echo "compare: $(grep '"verdict"' "$expected")"
