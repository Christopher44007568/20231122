#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-${root}/build}"
output_dir="${1:-${root}/artifacts/latest}"
backend="${BACKEND:-vulkan}"

cmake -S "${root}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "${build_dir}" -j
ctest --test-dir "${build_dir}" --output-on-failure
"${build_dir}/material_hotupdate" --backend "${backend}" --output "${output_dir}"
