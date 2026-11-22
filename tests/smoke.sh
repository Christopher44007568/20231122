#!/usr/bin/env bash
set -euo pipefail

exe=${1:?usage: smoke.sh /path/to/skeleton-kf}
work=${TMPDIR:-/tmp}/skeleton-kf-smoke-$$
trap 'rm -rf "$work"' EXIT

"$exe" demo --out "$work" --budget-mb 24 --pause-after-chunks 3 >/dev/null
"$exe" verify --record "$work/review-record.json"
"$exe" verify --record "$work/review-record.json" --strict >/dev/null
"$exe" reload-check --file "$work/review-record.json"
"$exe" run --out "$work/low" --budget-mb 2 --no-vulkan >/dev/null
"$exe" verify --record "$work/low/review-record.json" >/dev/null

test -s "$work/final-compressed.json"
test -s "$work/checkpoint.json"
test -s "$work/review-report.md"
test -s "$work/verification.json"
grep -q '"verdict": "MATCH"' "$work/review-record.json"
grep -q '"field_order_preserved": true' "$work/review-record.json"
grep -q '"budget_floor_exceeded": true' "$work/low/review-record.json"
