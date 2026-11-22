#!/usr/bin/env bash
set -euo pipefail

binary="${1:?binary path required}"
output="${2:?screenshot path required}"

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "no display server; skipping Vulkan GUI smoke test"
    exit 77
fi

mkdir -p "$(dirname "$output")"
timeout 60 "$binary" --select snap-v2 --screenshot "$output" --frames 10
test -s "$output"
