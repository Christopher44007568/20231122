# Local dependencies

- `vulkan/include/`: Khronos Vulkan-Headers (Apache-2.0 OR MIT).
- `glfw/`: GLFW 3.4 headers and null-platform static library (zlib license).

The GLFW library was built with `GLFW_PLATFORM_NULL`, Wayland and X11 disabled.
Only the offscreen Vulkan context initialization uses GLFW; no surface or
swapchain is created. The Vulkan headers are compile-time only; the executable
links against the system `libvulkan.so.1` loader and uses whichever ICD the
environment selects.
