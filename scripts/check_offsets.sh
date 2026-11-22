#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

printf 'header fields (decimal offsets):\n'
xxd -g 4 -l 48 materials/raw/outline_repeat.ssoraw
printf '\nduplicate originalOffset is at byte 284:\n'
xxd -g 4 -s 284 -l 12 materials/raw/outline_repeat.ssoraw
printf '\nfirst boundary record is at byte 320:\n'
xxd -g 4 -s 320 -l 16 materials/raw/outline_repeat.ssoraw
