#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VALIDATOR="${GLSLANG_VALIDATOR:-glslangValidator}"
if ! command -v "$VALIDATOR" >/dev/null 2>&1; then
    echo "glslangValidator not found; set GLSLANG_VALIDATOR=/path/to/glslangValidator" >&2
    exit 2
fi

"$VALIDATOR" -V shaders/instance.vert -o shaders/instance.vert.spv
"$VALIDATOR" -V shaders/instance.frag -o shaders/instance.frag.spv
echo "SPIR-V regenerated under shaders/"
