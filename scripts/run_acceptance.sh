#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

./scripts/build.sh
VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" \
  ./build/ssor accept --out artifacts/acceptance

printf '\nAcceptance report: %s\n' "$ROOT/artifacts/acceptance/acceptance_report.md"
printf 'Timeline:          %s\n' "$ROOT/artifacts/acceptance/evidence_timeline.csv"
