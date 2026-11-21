#!/usr/bin/env bash
# One-command reproducible review of the render pipeline state cache.
# Builds the tool, runs the full record -> truncate -> recover -> replay
# pipeline, and checks that the first erroneous state is pinpointed.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-artifacts/review}"

cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j >/dev/null

./build/rpsc review --out "$OUT"

echo
echo "== checks =="
grep -q "FIRST ERRONEOUS STATE" <(./build/rpsc replay \
    --trace "$OUT/recovered.rpsc2" --golden "$OUT/golden" \
    --frames /tmp/rpsc_check_frames --report /tmp/rpsc_check.json) \
  && echo "OK: first erroneous state reported"
python3 - "$OUT" <<'EOF'
import json, sys
out = sys.argv[1]
rep = json.load(open(f"{out}/replay_recovered.json"))
assert rep["firstErroneousState"] is not None, "no first erroneous state"
assert rep["firstErroneousState"]["expected"]["op"] == "BindPipeline"
rec = json.load(open(f"{out}/recovery_report.json"))
assert rec["firstDefect"]["reason"] == "TruncatedPayload"
assert rec["recordsRecovered"] < rec["recordsDeclared"]
legacy = json.load(open(f"{out}/replay_legacy.json"))
assert legacy["firstErroneousState"] is None, "legacy compat broken"
print("OK: recovery defect, first-error pinpointing and v1 compat verified")
EOF
echo "evidence: $OUT/REVIEW.md"
