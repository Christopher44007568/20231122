#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GLSLC="${GLSLC:-glslc}"

"$GLSLC" -fshader-stage=vert "$ROOT/shaders/ui.vert" -o "$ROOT/assets/ui.vert.spv"
"$GLSLC" -fshader-stage=frag "$ROOT/shaders/ui.frag" -o "$ROOT/assets/ui.frag.spv"

echo "compiled UI shaders into $ROOT/assets"
