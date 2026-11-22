#!/usr/bin/env bash
# Reproduces third_party/local/usr on a host without root access:
# downloads the Vulkan/GLFW development debs with apt-get download and
# extracts them into the project-local prefix used by CMake.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="$ROOT/third_party/local"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cd "$WORK"
apt-get download libvulkan-dev libglfw3-dev libglfw3 \
                 glslang-tools spirv-tools
mkdir -p "$PREFIX"
for deb in *.deb; do dpkg-deb -x "$deb" "$PREFIX"; done

# The dev package only ships the libvulkan.so linker symlink; copy the
# runtime target next to it so the prefix is self-contained.
if [ ! -e "$PREFIX/usr/lib/x86_64-linux-gnu/libvulkan.so.1" ]; then
  cp -L /usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
        "$PREFIX/usr/lib/x86_64-linux-gnu/"
fi
echo "deps ready under $PREFIX/usr"
