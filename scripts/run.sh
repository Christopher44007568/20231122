#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ ! -x "$BUILD_DIR/vfcr" ]]; then
    "$ROOT/scripts/build.sh"
fi

if [[ -n "${VFCR_DEPS_ROOT:-}" ]]; then
    export LD_LIBRARY_PATH="$VFCR_DEPS_ROOT/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
fi

cd "$ROOT"
exec "$BUILD_DIR/vfcr" "$@"
