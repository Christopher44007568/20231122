#!/usr/bin/env bash
set -euo pipefail

binary="${1:?binary path required}"
output="${2:?output path required}"

if [[ -z "${DISPLAY:-}" ]]; then
  echo "SKIP: no DISPLAY for GLFW/Vulkan window"
  exit 77
fi

mkdir -p "$(dirname "$output")"
timeout 30s env DISPLAY="${DISPLAY}" HDRPROV_FORCE_X11=1 \
  "$binary" --screenshot "$output" --frames 10 --select snap-v2

test -s "$output"
echo "GUI smoke screenshot: $output"
