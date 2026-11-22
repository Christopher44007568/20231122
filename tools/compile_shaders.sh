#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${GLSLANG:-glslangValidator}"
tmp="$(mktemp -d)"
trap 'rm -r "$tmp"' EXIT

"$compiler" -V --target-env vulkan1.0 -Os -g0 -S vert \
    --vn kSceneVertSpv -o "$tmp/scene.vert.spv" "$root/shaders/scene.vert"
"$compiler" -V --target-env vulkan1.0 -Os -g0 -S frag \
    --vn kSceneFragSpv -o "$tmp/scene.frag.spv" "$root/shaders/scene.frag"

{
    printf '#include "shaders_spv.hpp"\n\n'
    sed '1,2d' "$tmp/scene.vert.spv"
    printf '\n'
    sed '1,2d' "$tmp/scene.frag.spv"
    printf '\nconst std::size_t kSceneVertSpvWords = sizeof(kSceneVertSpv) / sizeof(kSceneVertSpv[0]);\n'
    printf 'const std::size_t kSceneFragSpvWords = sizeof(kSceneFragSpv) / sizeof(kSceneFragSpv[0]);\n'
} > "$root/src/shaders_spv.cpp"

echo "wrote $root/src/shaders_spv.cpp"
