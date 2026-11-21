#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="${root}/.deps"
if [[ -f "${deps}/.complete" ]]; then
  echo "local dependencies already present at ${deps}"
  exit 0
fi

packages=(
  libvulkan1
  libvulkan-dev
  libglfw3
  libglfw3-dev
)

mkdir -p "${deps}/debs" "${deps}/usr"
cd "${deps}/debs"

apt_args=()
if [[ -n "${APT_PROXY:-}" ]]; then
  apt_args+=("-o" "Acquire::http::Proxy=${APT_PROXY}")
  apt_args+=("-o" "Acquire::https::Proxy=${APT_PROXY}")
fi

apt-get "${apt_args[@]}" download "${packages[@]}"
for archive in ./*.deb; do
  dpkg-deb -x "${archive}" "${deps}"
done

if [[ ! -e "${deps}/usr/lib/x86_64-linux-gnu/libvulkan.so.1" &&
      -e /usr/lib/x86_64-linux-gnu/libvulkan.so.1 ]]; then
  ln -s /usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
    "${deps}/usr/lib/x86_64-linux-gnu/libvulkan.so.1"
fi

touch "${deps}/.complete"
echo "local dependencies extracted at ${deps}"
