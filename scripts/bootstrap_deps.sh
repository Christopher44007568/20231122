#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="$root/.deps"
downloads="$deps/downloads"
prefix="$deps"

mkdir -p "$downloads" "$prefix"
cd "$downloads"

packages=(
  libvulkan-dev
  libglfw3-dev
  libglfw3
  glslang-tools
  libstb-dev
)

for package in "${packages[@]}"; do
  if ! ls "${package}"_*.deb >/dev/null 2>&1; then
    apt-get download "$package"
  fi
done

for package in "${packages[@]}"; do
  for archive in "${package}"_*.deb; do
    dpkg-deb -x "$archive" "$prefix"
  done
done

ln -sfn /usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
  "$prefix/usr/lib/x86_64-linux-gnu/libvulkan.so"

echo "Local build dependencies are ready in $deps"
