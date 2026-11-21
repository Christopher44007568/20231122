#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

CMAKE_ARGS=()
if [[ -n "${VFCR_DEPS_ROOT:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$VFCR_DEPS_ROOT/usr")
    CMAKE_ARGS+=("-DVFCR_DEPS_ROOT=$VFCR_DEPS_ROOT")
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "built: $BUILD_DIR/vfcr"
