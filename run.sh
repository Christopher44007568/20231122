#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS="$ROOT/.deps/sysroot/usr/lib/x86_64-linux-gnu"

if [[ ! -x "$ROOT/build/thinline_provenance" ]]; then
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/build" -j"$(nproc)"
fi

export LD_LIBRARY_PATH="$DEPS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$ROOT/build/thinline_provenance" "$@"
