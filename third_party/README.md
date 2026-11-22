# Vendored dependencies

- `vulkan/include/`：Vulkan-Headers 1.4.341 C 头，来自 Ubuntu `libvulkan-dev 1.4.341.0-1`，许可证见 `vulkan/COPYRIGHT`（Apache-2.0）。
- `glfw/`：GLFW 3.4 头文件与 `libglfw.so.3`，来自 Ubuntu `libglfw3 3.4-4` / `libglfw3-dev 3.4-4`，许可证见 `glfw/COPYRIGHT`。

运行库由 `libvulkan.so.1` loader 和系统 Vulkan ICD 驱动。项目不修改这些上游文件。
