# Build guide

The fixed toolchain is C++17, Vulkan and GLFW. Everything required to build
is vendored under `third_party/` so the project builds without root access or
system package installation.

## Layout

- `third_party/vulkan/include` — Vulkan 1.3 headers
- `third_party/vulkan/lib/libvulkan.so.1` — Vulkan loader
- `third_party/glfw/include` — GLFW 3.4 headers
- `third_party/glfw/lib/libglfw.so.3.4` — GLFW shared library (dlopens X11
  at runtime, so it has no hard link-time X11 dependencies)

## Headless build (CLI + core library + tests)

```
cmake -S . -B build -DCAMTRAK_BUILD_GUI=OFF
cmake --build build -j
ctest --test-dir build
```

## Full build with the Vulkan/GLFW GUI

GLSL shaders are compiled to SPIR-V at build time. Point CMake at a
`glslangValidator` binary, for example one extracted locally from the
`glslang-tools` package:

```
cmake -S . -B build-gui \
  -DCAMTRAK_BUILD_GUI=ON \
  -DGLSLANG_VALIDATOR=/path/to/glslangValidator
cmake --build build-gui -j
```

If `TMPDIR` points at a small tmpfs that fills up during compilation, point
it at a project-local directory:

```
mkdir -p .tmp
TMPDIR=$PWD/.tmp cmake --build build-gui -j
```

## Artifacts

- `camtrak` — command line front end (all workflows are available headless)
- `camtrak_gui` — Vulkan/GLFW trajectory visualization
- `test_core` — unit tests, also registered with CTest
- `shaders/*.spv` — compiled SPIR-V modules next to the GUI binary
