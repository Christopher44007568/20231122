#!/bin/sh
# Regenerates all validation artifacts (requires lavapipe or a Vulkan GPU).
set -e
cd "$(dirname "$0")"
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
BIN=./build/outline_diag

mkdir -p artifacts/broken_lines/depth_strict \
         artifacts/broken_lines/normal_edge \
         artifacts/broken_lines/intersection

$BIN --dump artifacts
$BIN --dump artifacts/broken_lines/depth_strict --depth-thr 5
$BIN --dump artifacts/broken_lines/normal_edge --normal-thr 1.05
$BIN --dump artifacts/broken_lines/intersection --normal-thr 0.5
