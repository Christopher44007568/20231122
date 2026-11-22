#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
COMPILER=${GLSLANG_VALIDATOR:-glslangValidator}

"$COMPILER" -V --target-env vulkan1.1 \
    -o "$ROOT/shaders/fog.comp.spv" "$ROOT/shaders/fog.comp"
"$COMPILER" -V --target-env vulkan1.1 \
    -o "$ROOT/shaders/display.vert.spv" "$ROOT/shaders/display.vert"
"$COMPILER" -V --target-env vulkan1.1 \
    -o "$ROOT/shaders/display.frag.spv" "$ROOT/shaders/display.frag"

echo "SPIR-V shaders written to $ROOT/shaders"
