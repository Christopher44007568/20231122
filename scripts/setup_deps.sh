#!/usr/bin/env bash
# 无 root 权限时，把 Vulkan/GLFW 开发包与 glslang 工具解包到项目内 .localdeps/
# 有 root 时可直接: sudo apt-get install -y libvulkan-dev libglfw3-dev glslang-tools
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${ROOT_DIR}/.localdeps"
mkdir -p "${DEPS_DIR}"

APT_OPTS=(-o Acquire::http::Proxy=NONE -o Acquire::https::Proxy=NONE)

TMP_DIR="$(mktemp -d)"
cd "${TMP_DIR}"
apt-get download "${APT_OPTS[@]}" \
    libvulkan-dev libglfw3-dev libglfw3 glslang-tools spirv-tools
for deb in *.deb; do
    dpkg-deb -x "${deb}" "${DEPS_DIR}"
done
cd "${ROOT_DIR}"
rm -rf "${TMP_DIR}"

echo "依赖已解包到 ${DEPS_DIR}"
echo "构建: cmake -S . -B build && cmake --build build"
