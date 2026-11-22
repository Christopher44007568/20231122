#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/out"

if [[ -z "${VK_ICD_FILENAMES:-}" ]]; then
  if [[ -f /usr/share/vulkan/icd.d/lvp_icd.json ]]; then
    export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
    echo "[run.sh] 未检测到 VK_ICD_FILENAMES，回退到 lvp 软件驱动: ${VK_ICD_FILENAMES}"
  fi
fi

if [[ ! -x "${BUILD_DIR}/occlusion_audit" ]]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "${BUILD_DIR}" -j

mkdir -p "${OUT_DIR}"
"${BUILD_DIR}/occlusion_audit" --out "${OUT_DIR}"

echo
echo "来源检查页: ${OUT_DIR}/source_check.html"
echo "机器账本:   ${OUT_DIR}/audit_report.json"
echo "原始输入:   ${OUT_DIR}/raw_input_ledger.txt"
