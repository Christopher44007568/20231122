#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-build/gs_inspect}"
OUT="${2:-evidence/resume-chain-$(date -u +%Y%m%dT%H%M%SZ)}"

if [[ ! -x "${BIN}" ]]; then
    echo "missing executable: ${BIN}" >&2
    echo "build it with: cmake -S . -B build && cmake --build build -j" >&2
    exit 2
fi
if [[ -e "${OUT}/state.json" ]]; then
    OUT="${OUT}-$(date -u +%Y%m%dT%H%M%SZ)"
    echo "previous evidence preserved; new run: ${OUT}"
fi

export GS_HEADLESS=1
set +e
"${BIN}" --run "${OUT}"
run_code=$?
"${BIN}" --resume "${OUT}"
first_resume_code=$?
"${BIN}" --resume "${OUT}"
second_resume_code=$?
set -e

if [[ ${run_code} -ne 20 || ${first_resume_code} -ne 20 ||
      ${second_resume_code} -ne 0 ]]; then
    echo "unexpected exit codes: ${run_code}, ${first_resume_code}, ${second_resume_code}" >&2
    exit 1
fi

if ! grep -q '"accepted": true' "${OUT}/acceptance.json"; then
    echo "acceptance failed; inspect ${OUT}" >&2
    exit 1
fi

echo "resume chain passed"
echo "evidence: ${OUT}/acceptance.json"
echo "timeline: ${OUT}/timeline.jsonl"
