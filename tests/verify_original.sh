#!/usr/bin/env bash
set -euo pipefail

root="${1:?source root required}"
scene="${root}/assets/original_scene.json"
sums="${root}/assets/SHA256SUMS"

test -f "${scene}"
test -f "${sums}"
test "$(stat -c '%a' "${scene}")" = "644"
test "$(sha256sum "${scene}" | cut -d' ' -f1)" = \
  "$(awk '{print $1}' "${sums}")"
grep -q '"schema": "gbi.scene/1"' "${scene}"
grep -q '"id": "cube"' "${scene}"
grep -q '"id": "panel"' "${scene}"
grep -q '"id": "prism"' "${scene}"

echo "original material is present and structurally intact"
