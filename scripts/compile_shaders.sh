#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${GLSLANG_VALIDATOR:-glslangValidator}"
OUT="${ROOT}/build/shaders"
mkdir -p "$OUT"
"$COMPILER" -V --target-env vulkan1.1 \
    -o "$OUT/mesh.vert.spv" "$ROOT/shaders/mesh.vert"
"$COMPILER" -V --target-env vulkan1.1 \
    -o "$OUT/mesh.frag.spv" "$ROOT/shaders/mesh.frag"
echo "compiled to $OUT"
