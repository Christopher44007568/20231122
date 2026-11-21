#!/usr/bin/env bash
# Root-free dependency bootstrap for a minimal Ubuntu container:
#   * Vulkan-Headers (Khronos, headers only)
#   * GLFW 3.4 built as a static library on the null platform (no X11/Wayland)
#   * glslang built from source for glslangValidator (GLSL -> SPIR-V)
# The Vulkan loader (libvulkan.so.1) and lavapipe driver are expected from
# system packages (libvulkan1, mesa-vulkan-drivers).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TP="$ROOT/third_party"
mkdir -p "$TP"

if [[ ! -f "$TP/vulkan-headers/include/vulkan/vulkan.h" ]]; then
    git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git \
        "$TP/vulkan-headers"
fi

if [[ ! -f "$TP/glfw/install/lib/libglfw3.a" ]]; then
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git \
        "$TP/glfw"
    cmake -S "$TP/glfw" -B "$TP/glfw/build-null" \
        -DCMAKE_INSTALL_PREFIX="$TP/glfw/install" \
        -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF \
        -DGLFW_BUILD_DOCS=OFF -DGLFW_PLATFORM=GLFW_PLATFORM_NULL \
        -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=OFF \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$TP/glfw/build-null" -j"$(nproc)" --target install
fi

if [[ ! -x "$TP/glslang/install/bin/glslangValidator" ]]; then
    git clone --depth 1 https://github.com/KhronosGroup/glslang.git \
        "$TP/glslang"
    git clone --depth 1 https://github.com/KhronosGroup/SPIRV-Tools.git \
        "$TP/glslang/External/spirv-tools"
    git clone --depth 1 https://github.com/KhronosGroup/SPIRV-Headers.git \
        "$TP/glslang/External/spirv-tools/external/spirv-headers"
    cmake -S "$TP/glslang" -B "$TP/glslang/build" \
        -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=OFF \
        -DBUILD_TESTING=OFF -DGLSLANG_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX="$TP/glslang/install"
    cmake --build "$TP/glslang/build" -j"$(nproc)"
    cmake --install "$TP/glslang/build"
fi

echo "dependencies ready under $TP"
