#!/usr/bin/env bash
set -euo pipefail

# Regenerates the checked-in SPIR-V header. The normal build does not need
# glslangValidator because src/pose_error_spv.inc is committed.

validator=${GLSLANG_VALIDATOR:-$(command -v glslangValidator || true)}
if [[ -z "$validator" ]]; then
  validator=.deps/root/usr/bin/glslangValidator
fi
if [[ ! -x "$validator" ]]; then
  echo "glslangValidator not found; set GLSLANG_VALIDATOR" >&2
  exit 1
fi

spv=$(mktemp "${TMPDIR:-/tmp}/pose-error.XXXXXX.spv")
trap 'rm -f "$spv"' EXIT
"$validator" -V shaders/pose_error.comp -o "$spv"

{
  printf '// Generated from shaders/pose_error.comp by glslangValidator.\n'
  printf '#pragma once\n#include <cstdint>\n'
  printf 'namespace kfc { constexpr std::uint32_t kPoseErrorSpirv[] = {\n'
  od -An -v -t x4 "$spv" | tr -s ' ' '\n' | sed '/^$/d;s/^/  0x/;s/$/,/'
  printf '};\n}\n'
} > src/pose_error_spv.inc

echo "updated src/pose_error_spv.inc"
