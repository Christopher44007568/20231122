# Rasterization Rule Regression

一个可复现的 Vulkan 光栅化规则回归工具。固定技术栈为 **C++17 + Vulkan 1.1 + GLFW 3.3+**。渲染走离屏 framebuffer，GLFW 负责平台初始化、Vulkan 可用性探测和可选的预览窗口，因此无窗口环境也可以做回归。

工具把输入拆成四段证据链：

- 光栅化规则：填充/线/点、cull、front face、viewport、scissor、线宽、depth bias、discard、additive blend。
- 共享边：两个 draw 的共享边端点，以及最终帧在边中点附近的像素探针。
- 参考像素：按 pass 指定的 RGBA8 期望值，支持逐通道 tolerance。
- 差异：逐 draw 的 framebuffer 前后哈希、变化像素数、最大通道差、首个差异点、bbox，以及 pass 级 PPM/PAM 和边界前后图像。

## 快速开始

系统已安装 `libvulkan-dev`、`libglfw3-dev`、`glslang-tools` 时：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

当前机器没有开发包时，可以只解包用户目录，不需要 root：

```bash
bash scripts/bootstrap_local_deps.sh
# 使用脚本最后打印的 cmake 命令配置
```

运行单个场景：

```bash
./build/raster-regression \
  --case fixtures/shared_edge.json \
  --out artifacts/regression-shared-edge
```

运行完整压力集：

```bash
./build/raster-regression --stress --out artifacts/regression-stress
```

压力集包含：空文件、共享边、线宽阈值两侧、viewport 阈值两侧、极端参数、664 KiB/8000 参考像素的大输入，以及 1 MiB + 4096 字节的超大输入拒绝路径。

## 输入格式

根字段：

| 字段 | 说明 |
| --- | --- |
| `schema` | 固定为 `raster-regression/v1` |
| `name` | 场景名 |
| `framebuffer` | `width`、`height`，1..4096 |
| `clear_color` | RGBA8，缺省 `[16,16,24,255]` |
| `vertices` | `position` 为 `[x,y]` 或 `[x,y,z,w]`，`rgba` 为 RGBA8 |
| `indices` | uint32 索引；省略时按顶点顺序生成 |
| `rules` / `rasterization_rules` | 光栅化规则表 |
| `draws` | indexed draw 范围 |
| `passes` | 一个 rule + 一组 draw，按顺序执行 |
| `shared_edges` | 共享边元数据，用于最终帧探针 |
| `reference_pixels` | 期望最终像素 |
| `boundary` | 指定阈值、前后 pass 和可选采样像素 |

最小规则示例：

```json
{
  "id": "baseline",
  "polygon_mode": "fill",
  "cull_mode": "none",
  "front_face": "ccw",
  "line_width": 1.0,
  "depth_clamp": false,
  "rasterizer_discard": false,
  "depth_bias_constant": 0.0,
  "depth_bias_slope": 0.0,
  "depth_bias_clamp": 0.0,
  "viewport_x": 0.0,
  "viewport_y": 0.0,
  "viewport_width": -1.0,
  "viewport_height": -1.0,
  "scissor_x": 0.0,
  "scissor_y": 0.0,
  "scissor_width": -1.0,
  "scissor_height": -1.0,
  "additive_blend": false
}
```

`viewport_width/height` 或 `scissor_width/height` 为负数时使用 framebuffer 尺寸。实际管线状态若被设备能力限制，会同时记录 requested/effective 值和 warning，例如线宽被钳到 `lineWidthRange`，或不支持 `fillModeNonSolid` 时回退到 fill。

pass 与 draw：

```json
"draws": [
  { "id": "left",  "first_index": 0, "index_count": 3 },
  { "id": "right", "first_index": 3, "index_count": 3 }
],
"passes": [
  { "id": "baseline", "rule": "baseline", "draws": ["left", "right"] }
]
```

共享边：

```json
"shared_edges": [
  {
    "id": "diagonal",
    "draw_a": "left",
    "draw_b": "right",
    "a": [0.8, -0.6],
    "b": [-0.2, 0.6]
  }
]
```

共享边端点使用 NDC，工具按 effective viewport 转换到 framebuffer，并在中点以及上下左右 1 像素处采样。它不会修改 Vulkan 的 top-left 规则，而是把规则执行结果固化成可比较证据。

参考像素：

```json
"reference_pixels": [
  {
    "pass": "baseline",
    "x": 16,
    "y": 26,
    "rgba": [255, 0, 0, 255],
    "tolerance": 1
  }
]
```

`pass` 省略时该参考像素应用于所有 pass。任意通道差值大于 `tolerance` 即失败，进程退出码为 2。

边界：

```json
"boundary": {
  "parameter": "viewport_x",
  "before_pass": "before",
  "after_pass": "after",
  "threshold": 0.0,
  "sample_pixel": [21, 38]
}
```

工具保存 `before.ppm`、`after.ppm`、`diff.ppm`，并记录整帧差异、阈值两侧的 requested/effective 参数值，以及采样像素在前后画面中的 RGBA 和通道差。

## 证据目录

```text
artifacts/regression-.../
├── manifest.json
├── evidence.sha256
├── input/input.json
├── boundary/{before,after,diff}.ppm
└── passes/<序号>-<pass>/
    ├── frame.ppm
    ├── frame.pam
    ├── change-from-clear.ppm
    └── draw-<序号>-after.ppm
```

`manifest.json` 的关键对应关系：

- `scenario`：输入规模与 framebuffer。
- `environment`：C++17、GLFW、Vulkan loader/driver、设备能力。
- `passes[].pipeline_state`：effective 光栅化规则。
- `passes[].resources`：vertex/index buffer 字节数、附件格式、layout 顺序。
- `passes[].draw_calls[]`：`vkCmdDrawIndexed` 参数、draw 前后 SHA-256、变化像素、首个差异点、bbox、保存的关键帧。
- `passes[].reference_pixels`：总数、失败数、报告样本；超过 4096 条时明确记录 `omitted`。
- `shared_edges[].samples[]`：每个 pass 的边中点探针。
- `boundary`：阈值前后值、整帧差异和采样像素变化。
- `artifacts[]`：每个证据文件的大小和 SHA-256；`evidence.sha256` 另含 manifest 自身哈希。

`draw_calls[]` 的变化是“该 draw 执行前后的 framebuffer 差异”，因此画面上看不到变化时仍会保留两个不同或相同的 SHA-256，便于定位资源状态与 draw 顺序。

## 空输入与超大输入

- 0 字节或纯空白输入：生成 16x16 的空场景，状态 `ok`，manifest 保留 SHA-256、空输入标记和 warning。
- 超过 `--max-input-bytes`：不读入内存、不初始化渲染，状态 `input_rejected`；保留文件大小、限额、前 64 KiB 哈希和拒绝说明。
- 大但未超限：继续解析和渲染；证据中的参考像素报告数量有上限，但总数、失败数和 omitted 数完整保留。

默认限额是 64 MiB；`--max-input-bytes` 最低可设为 1024。其他硬限额见 `src/model.hpp`，包括 framebuffer 4096x4096、单场景 1,000,000 顶点、4,000,000 索引和 100,000 次 draw submission。

## 退出码与状态

| 退出码 | 状态 | 含义 |
| --- | --- | --- |
| 0 | `ok` | 所有 pass 渲染完成，参考像素全部通过 |
| 2 | `reference_mismatch` | 渲染完成，但存在参考像素失败 |
| 1 | `input_rejected` / `input_error` / `runtime_error` / `partial` / `evidence_error` | 输入或运行阶段失败 |

## 测试覆盖

`ctest` 覆盖 JSON 解析和拒绝规则、UTF-8 转义、SHA-256 向量、空输入、模型校验、图像差异、参考像素 tolerance 和 PPM 写出。Vulkan 路径由 `--stress` 做端到端验证；默认使用系统可用设备，可用 `--device-name` 或 `--device-index` 固定设备。
