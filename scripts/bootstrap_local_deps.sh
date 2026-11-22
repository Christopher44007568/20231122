#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="${RR_DEPS_DIR:-${root}/.deps}"
mkdir -p "${deps}/packages"

cd "${deps}/packages"
apt-get download \
    libvulkan-dev \
    libglfw3-dev \
    libglfw3 \
    glslang-tools \
    spirv-tools

for package in ./*.deb; do
    dpkg-deb -x "${package}" "${deps}"
done

multiarch="$(gcc -print-multiarch)"
vulkan_runtime="$(ldconfig -p | awk '/libvulkan\.so\.1 / {print $NF; exit}')"
if [[ -n "${vulkan_runtime}" ]]; then
    ln -sfn "${vulkan_runtime}" "${deps}/usr/lib/${multiarch}/libvulkan.so.1"
fi

cat <<EOF
Local dependencies extracted to ${deps}.

Configure with:
  cmake -S "${root}" -B "${root}/build" \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DCMAKE_PREFIX_PATH="${deps}/usr" \\
    -DVulkan_LIBRARY="${deps}/usr/lib/${multiarch}/libvulkan.so" \\
    -DVulkan_INCLUDE_DIR="${deps}/usr/include" \\
    -DRR_GLSLANG_VALIDATOR="${deps}/usr/bin/glslangValidator"

Run with:
  LD_LIBRARY_PATH="${deps}/usr/lib/${multiarch}:\${LD_LIBRARY_PATH:-}" \\
    "${root}/build/raster-regression" --stress --out "${root}/artifacts/regression-stress"
EOF
