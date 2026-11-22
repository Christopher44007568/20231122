#!/usr/bin/env bash
set -euo pipefail

# Installs the two development packages into .deps/root without requiring root.
# Prefer distro packages when they are already available:
#   sudo apt install libvulkan-dev libglfw3-dev
#
# Usage: scripts/bootstrap_deps.sh [output-root]

root=${1:-.deps/root}
pkgdir=.deps/pkgs
mkdir -p "$pkgdir" "$root"

if [[ -f "$root/usr/include/vulkan/vulkan.h" &&
      -f "$root/usr/include/GLFW/glfw3.h" &&
      -e "$root/usr/lib/x86_64-linux-gnu/libglfw.so" ]]; then
  echo "local dependencies already present in $root"
  exit 0
fi

if ! command -v apt-get >/dev/null || ! command -v dpkg-deb >/dev/null; then
  echo "apt-get/dpkg-deb not found; install libvulkan-dev and libglfw3-dev" >&2
  exit 1
fi

apt_args=()
if [[ -n "${APT_PROXY:-}" ]]; then
  apt_args+=(
    -o "Acquire::http::Proxy=${APT_PROXY}"
    -o "Acquire::https::Proxy=${APT_PROXY}"
  )
fi

(
  cd "$pkgdir"
  env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY \
    apt-get "${apt_args[@]}" download libvulkan-dev libglfw3-dev libglfw3
)

for package in "$pkgdir"/*.deb; do
  dpkg-deb -x "$package" "$root"
done

libdir="$root/usr/lib/x86_64-linux-gnu"
if [[ ! -e "$libdir/libvulkan.so.1" ]]; then
  system_loader=$(ldconfig -p 2>/dev/null |
    awk '/libvulkan\.so\.1 / {print $NF; exit}')
  if [[ -n "$system_loader" ]]; then
    ln -s "$system_loader" "$libdir/libvulkan.so.1"
  fi
fi

echo "dependencies extracted into $root"
echo "configure with: cmake -S . -B build -G Ninja"
