#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VALIDATOR="${GLSLANG_VALIDATOR:-$(command -v glslangValidator || true)}"
if [[ -z "$VALIDATOR" ]]; then
  for candidate in /home/hj/local/usr/bin/glslangValidator /usr/bin/glslangValidator; do
    if [[ -x "$candidate" ]]; then VALIDATOR="$candidate"; break; fi
  done
fi
if [[ -z "$VALIDATOR" ]]; then
  echo "glslangValidator not found; set GLSLANG_VALIDATOR" >&2
  exit 1
fi

mkdir -p "$ROOT/shaders/spv"
for shader in geom.vert geom.frag fullscreen.vert edge.frag final.frag; do
  "$VALIDATOR" -V --target-env vulkan1.1 \
    -o "$ROOT/shaders/spv/${shader}.spv" "$ROOT/shaders/${shader}"
  echo "compiled ${shader} -> shaders/spv/${shader}.spv"
done
"$ROOT/scripts/embed_shaders.py" "$ROOT"
