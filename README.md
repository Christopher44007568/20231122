# 材质球能量对照

这是一个可复现的离屏诊断程序，用同一材质球网格的 8 个实例重建“材质球能量对照”过程。程序读取材质参数、反射球和能量积分，渲染基线帧与异常帧，并将 draw call、Vulkan 资源状态、framebuffer 字节和画面差异写入同一份证据报告。

技术栈固定为 **C++17 + Vulkan 1.1 + GLFW 3.4**。GLFW 使用隐藏的 Null platform 窗口，不依赖可见桌面；Vulkan 使用离屏 color/depth 附件，不需要 swapchain。

## 快速运行

```bash
./scripts/build.sh
./scripts/run_acceptance.sh
```

`run_acceptance.sh` 会构建、执行自测、校验原始材料哈希，并生成：

- `evidence/acceptance/report.json`：机器可读证据链
- `evidence/acceptance/acceptance.md`：带实际 UTC 时间点与原始偏移的验收记录
- `docs/ACCEPTANCE.md`：本次验收记录的稳定副本
- `evidence/acceptance/frames/*.rgba`：两帧原始 RGBA framebuffer
- `evidence/acceptance/frames/*.png`：基线、异常和差异可视化
- `evidence/acceptance/original/original.material`：未修改的原始材料副本

也可单独运行：

```bash
./build/msphere_energy --validate materials/original.material
./build/msphere_energy --render materials/original.material --out evidence/manual
./build/msphere_energy --self-test --out evidence/acceptance
```

## 输入

输入是 `MSPHERE1` 二进制包，包含 64 字节头、固定 32 字节材质记录、RGBA8 等距柱状反射球，以及每个材质一个 `float32` 能量积分。示例原始材料是 `materials/original.material`，SHA-256 记录在 `materials/original.sha256`。

完整字段、字节序和偏移规则见 `docs/FORMAT.md`。校验器在读取大文件前先检查文件大小，空输入返回 `empty@0`，超过 8 MiB 的输入返回 `too_large@8388608`，不会尝试分配对应大小的缓冲区。

## 异常场景

每个材质对应一个重复球体实例，每帧只发出一个 `vkCmdDrawIndexed`：

```text
firstInstance = 0, instanceCount = 8, firstIndex = 0, indexCount = 12288
```

异常只注入 GPU 上传副本：实例 5 的 `base_color.r` 从原始字节 `1f856b3f` 改为 `cdcccc3c`。原始文件偏移是 `224`，GPU material buffer 偏移是 `160`。基线帧与异常帧共享同一个 color image/framebuffer，但 material buffer 内容哈希不同。

报告中的 `correlation` 对象把以下证据串成一条链：

```text
draw sequence 2
  -> first_instance 0 + relative instance 5
  -> source material offset 224 / GPU buffer offset 160
  -> framebuffer and color image handles
  -> first differing raw RGBA byte offset 293324
  -> difference bounds and instance screen cell
```

## 验证

```bash
ctest --test-dir build --output-on-failure
```

自测检查 12 项：输入偏移、原始材料字节不变、重复实例 draw call、两帧 GPU 时间戳、framebuffer 尺寸、差异非零且定位在异常实例单元、能量相对变化、空/超大输入拒绝、反射球最终布局。验收值以 `evidence/acceptance/report.json` 和 `docs/ACCEPTANCE.md` 中的实际时间点、GPU tick、CPU 微秒和字节偏移为准。

## 目录

- `src/format.*`：二进制格式解析与边界校验
- `src/renderer.*`：Vulkan 离屏渲染、时间戳、资源证据
- `src/image.*`：RGBA 回读、PNG、逐像素差异
- `src/report.*`：JSON 与 Markdown 证据输出
- `src/main.cpp`：CLI、自测、空/超大输入流程
- `shaders/`：GLSL 源码；`assets/shaders/`：已编译 SPIR-V
- `third_party/`：Vulkan C 头与 GLFW 3.4 运行库
