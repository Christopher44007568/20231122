#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${GLSLANG_VALIDATOR:-glslangValidator}"

"${compiler}" -V --target-env vulkan1.1 \
  -o "${root}/assets/shaders/material.vert.spv" \
  "${root}/shaders/material.vert"
"${compiler}" -V --target-env vulkan1.1 \
  -o "${root}/assets/shaders/material.frag.spv" \
  "${root}/shaders/material.frag"
