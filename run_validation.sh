#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
cd "$root"

cmake --build build -j
./build/surface_unfold --verify
ctest --test-dir build --output-on-failure

if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    timeout 90 ./build/surface_unfold --select snap-v2 \
        --screenshot artifacts/screenshots/main.png --frames 10
fi

test -s artifacts/evidence/manifest.json
test -s artifacts/evidence/boundary_seam.hex
test ! -e .tmp || test -z "$(find .tmp -mindepth 1 -print -quit)"
echo "artifact and cleanup checks passed"
