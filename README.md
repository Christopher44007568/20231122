# 网格边界标记 · 可复核渲染流程

技术栈固定为 **C++17 + Vulkan + GLFW**。程序把“网格外轮廓边界标记”做成一条
可复核流水线：每帧的 draw call、资源屏障（image layout）、离屏帧缓冲读回与
画面差异全部落盘，自动结论与人工确认单分开输出，并覆盖一次“中途取消”。

## 构建

有 root 权限：

```bash
sudo apt-get install -y libvulkan-dev libglfw3-dev glslang-tools cmake g++
cmake -S . -B build && cmake --build build -j
```

无 root 权限（本机场景）：先把开发包解包到项目内，再正常构建。

```bash
./scripts/setup_deps.sh
cmake -S . -B build && cmake --build build -j
```

运行（在构建目录内，SPIR-V 与工作目录约定如此）：

```bash
cd build
./grid_marker --config ../config/demo_v2.json --headless
./grid_marker --config ../config/demo_v1_legacy.json --headless
```

## 配置格式（旧/新兼容）

- 旧格式 v1（`config/demo_v1_legacy.json`）：扁平字段 `width/height/grid/frames/
  line_color`；`grid` 支持数字（方阵）或 `[列,行]`；无 `format` 字段时默认按 v1。
- 新格式 v2（`config/demo_v2.json`）：嵌套 `render.* / grid.* / run.* / cancel.*`，
  支持 `cancel.enabled + cancel.after_frame` 表达中途取消。
- 归一化结果写入每次运行的 `events.log` 与 `manifest.json`，原始字节存为
  `config.raw.json`，保证复核时能对照输入与解析结果。

## 一帧的可复核对应关系

每帧恰好两个 draw call，事件逐条写入 `events.log`：

| draw | 语义 | 拓扑 | 提交 |
|---|---|---|---|
| draw#0 | 背景全屏三角形 | TRIANGLE_LIST | `vkCmdDraw(3,1,0,0)` |
| draw#1 | 网格外轮廓边界 | LINE_LIST | `vkCmdDraw(8, 列*行,0,0)` |

资源状态链路（同样逐条记录屏障）：

`colorImage: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL`，
经 `vkCmdBlitImage` 拷到 host visible 线性 image 后映射回 CPU，保存为 PPM。

画面证据：每帧保存 `frames/frame_XXXX.ppm`，同时计算
截图文件 SHA-256 与纯像素 SHA-256、边界像素数、与上一帧差异像素数。

## 中途取消语义

`cancel.after_frame=N` 表示第 N 帧（1 基）完成并保留证据后，不再提交新帧；
已在途的 GPU 工作均等待完成，不截断已产生的证据。演示配置为 6 帧计划、
第 3 帧后取消，因此只产出 3 张截图，自动报告会核验该数量。

## 证据目录与报告

每次运行生成 `artifacts/runs/run-<时间戳>-<后缀>/`：

- `REPORT_auto.md`：仅陈述程序可验证事实（draw call、屏障、指纹、自动核验项）。
- `REPORT_manual.md`：人工复核确认单，检查项与证据索引，结论由人填写。
- `manifest.json`：逐帧结构化证据（指纹、像素统计、最终 layout）。
- `events.log`：带时间戳的流式事件，异常退出也保留已写入部分。
- `frames/*.ppm`：逐帧截图；`config.raw.json`：原始配置。
- `FAILURE.md`：仅在失败时生成，含本次原因与按顺序可执行的排查动作。

## 设计要点

- push constant 全部使用 16 字节对齐的 `vec4`，避免 std140 混合对齐歧义。
- 网格整体内缩半像素，避免外轮廓落在视口像素边界被光栅化规则剔除。
- 非轮廓边在所属单元内退化为零长度线段，单 draw 即可提交全部边界且无杂线。
- 无窗口/无显示环境可用 `--headless`（GLFW 仅做平台初始化，不创建窗口）；
  验证层不可用时在事件中如实记录 `validation=off`，不伪造状态。
