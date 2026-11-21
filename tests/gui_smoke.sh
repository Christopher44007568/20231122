#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "no display server; skipping GUI smoke test"
    exit 77
fi

TARGET="${1:-build/curved_trim}"
WORKSPACE="$(mktemp -d /tmp/clipfix-gui.XXXXXX)"
SHOT="$(mktemp /tmp/clipfix-gui-shot.XXXXXX.png)"

"${TARGET}" gui --workspace "${WORKSPACE}" --frames 4 \
    --screenshot "${SHOT}" --shot-frame 2

test -s "${SHOT}"
python3 - "${SHOT}" <<'PY'
import pathlib
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
assert data.startswith(b"\x89PNG\r\n\x1a\n"), "screenshot is not a PNG"
assert len(data) > 4096, "screenshot looks empty"
print("gui smoke: PASS", len(data), "bytes")
PY
