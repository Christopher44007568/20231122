#!/usr/bin/env bash
# Bootstraps the third-party dependencies into ${DEPS_ROOT} (default
# ~/deps): Vulkan-Headers, GLFW 3.4 (null platform, headless) and
# glslangValidator. Requires: cmake, a C++ compiler, curl, and the
# Vulkan loader (libvulkan1) already installed on the system.
set -euo pipefail

DEPS_ROOT="${DEPS_ROOT:-$HOME/deps}"
SRC="$DEPS_ROOT/src"
VKH_VER="1.4.341"
GLFW_VER="3.4"
GLSLANG_VER="15.4.0"
JOBS="$(nproc)"

mkdir -p "$SRC" "$DEPS_ROOT/include" "$DEPS_ROOT/lib" "$DEPS_ROOT/bin"
cd "$SRC"

fetch() {  # fetch <url> <out.tar.gz>
  curl -sL --retry 3 -o "$2" "$1"
}

if [ ! -d "Vulkan-Headers-$VKH_VER" ]; then
  fetch "https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v$VKH_VER" vkh.tar.gz
  tar xzf vkh.tar.gz
fi
cp -r "Vulkan-Headers-$VKH_VER/include/"* "$DEPS_ROOT/include/"

if [ ! -d "glfw-$GLFW_VER" ]; then
  fetch "https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/$GLFW_VER" glfw.tar.gz
  tar xzf glfw.tar.gz
fi
if [ ! -f "$DEPS_ROOT/lib/libglfw3.a" ]; then
  cmake -S "glfw-$GLFW_VER" -B "glfw-$GLFW_VER/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=OFF \
    -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
    -DCMAKE_INSTALL_PREFIX="$DEPS_ROOT"
  cmake --build "glfw-$GLFW_VER/build" -j"$JOBS"
  cmake --install "glfw-$GLFW_VER/build"
fi

if [ ! -d "glslang-$GLSLANG_VER" ]; then
  fetch "https://codeload.github.com/KhronosGroup/glslang/tar.gz/refs/tags/$GLSLANG_VER" glslang.tar.gz
  tar xzf glslang.tar.gz
fi
if [ ! -x "$DEPS_ROOT/bin/glslangValidator" ]; then
  cmake -S "glslang-$GLSLANG_VER" -B "glslang-$GLSLANG_VER/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_GLSLANG_BINARIES=ON -DENABLE_GLSLANG_JS=OFF -DENABLE_HLSL=OFF \
    -DENABLE_SPVREMAPPER=OFF -DENABLE_OPT=OFF -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX="$DEPS_ROOT"
  cmake --build "glslang-$GLSLANG_VER/build" -j"$JOBS"
  cmake --install "glslang-$GLSLANG_VER/build"
fi

# Link against the system Vulkan loader without root privileges.
if [ ! -e "$DEPS_ROOT/lib/libvulkan.so" ]; then
  ln -s /usr/lib/x86_64-linux-gnu/libvulkan.so.1 "$DEPS_ROOT/lib/libvulkan.so"
fi

echo "deps ready at $DEPS_ROOT"
