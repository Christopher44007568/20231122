#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
cd "$root"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
./build/surface_unfold_fixtures .
./build/surface_unfold --verify

if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    mkdir -p artifacts/screenshots
    timeout 90 ./build/surface_unfold --select snap-v2 \
        --screenshot artifacts/screenshots/main.png --frames 10
    timeout 90 ./build/surface_unfold --select snap-v1 \
        --screenshot artifacts/screenshots/snap-v1.png --frames 10
    timeout 90 ./build/surface_unfold --select unknown-uvx \
        --screenshot artifacts/screenshots/unknown.png --frames 10
else
    echo "no display server; skipping Vulkan screenshots"
fi

ctest --test-dir build --output-on-failure
test ! -e .tmp || test -z "$(find .tmp -mindepth 1 -print -quit)"
echo "validation complete; temporary state is clean"
