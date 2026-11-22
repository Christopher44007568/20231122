# 曲面参数化展开 · 来源检查页

固定技术栈：**C++17 + Vulkan + GLFW**。这是一个本地 Vulkan 检查页，用一次
批处理把同一曲面的两个版本快照、一个未知扩展对象和一条无法判断的接缝边界
冻结在同一条证据链里。

![来源检查页](artifacts/screenshots/main.png)

## 检查对象

| 对象 | 输入 | 结论 |
| --- | --- | --- |
| `snap-v1` | `data/snapshots/ribbed_sheet_v1.suf` | `PASS`，作为只读基线 |
| `unknown-uvx` | `data/records/seam_probe.uvx` | 未知扩展，`UnknownFormatError` 隔离 |
| `snap-v2` | `data/snapshots/ribbed_sheet_v2.suf` | 展开可解析，但接缝 `B-03` 为 `UNDECIDABLE` |

处理顺序是 v1 → unknown → v2。未知对象抛出异常后，对象级 `try/catch`
保留原始 96 B、输入 SHA-256 与头前缀，然后继续处理 v2。

## 页面结构

- 左侧按实际处理顺序列出对象，显示扩展名、大小、SHA-256 与状态。
- 中间从真实输入字节解码三维曲面和 UV 展开；Vulkan 纹理显示的是解码结果，
  不是占位图。`原始证据` 可切到 E01/E02/E05/E06 的十六进制原文。
- 右侧是 `S01…S07` 处理步骤与 `E01…E06` 证据引用。每个结论底部的
  `STEPS / EVIDENCE` 胶囊都能回到本对象的步骤和字节区间。
- `B-03` 单独呈现 `orientation=0`、`tolerance=0.002`、forward/reverse
  两个候选误差，并明确写出“无法判断”。

快捷键：`↑/↓` 切换对象，`Tab` 切换预览/原始字节，`R` 重新验证，`Esc` 退出。

## 构建与运行

依赖已放在 `third_party/`（Vulkan 头文件与 loader、GLFW 3.4、Dear ImGui、
STB、nlohmann/json），字体在 `assets/fonts/`，不需要 root 或系统开发包。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j

# 无窗口验证：生成证据并检查异常隔离、边界和清理
./build/surface_unfold --verify

# Vulkan 交互页
./build/surface_unfold

# 固定帧数截图
./build/surface_unfold --select snap-v2 \
  --screenshot artifacts/screenshots/main.png --frames 10
```

一键执行（构建、生成 fixture、验证、截图、CTest）：

```bash
./run.sh
```

## 快照格式

`SUF1` / `SUF2` 是小端二进制容器：

```text
offset  0  magic[4] = "SUF1" | "SUF2"
offset  4  uint16 version
offset  6  uint16 vertexCount
offset  8  uint16 triangleCount
offset 10  uint16 seamCount
offset 12  float  uvScale
offset 16  vertex[vertexCount]  = 3 x float position + 2 x float uv
...        uint16 index[triangleCount * 3]
...        seam[seamCount]       = a, b, orientation, confidence,
                                   forwardAreaError, reverseAreaError
```

v1 没有接缝表；v2 增加接缝表。格式分派只看 magic，不看扩展名。

`B-03` 是 v2 的第 3 条接缝：`orientation=0`、`confidence=0`，
forward 误差 `-0.00035`、reverse 误差 `+0.00035`，两者绝对值都小于
`0.002` 且符号相反。输入没有翻转提示、相邻岛或人工标注，因此不能裁决。
页面只保留这个不确定结论和 E05/E06 原文，不把任一候选写成最终展开。

## 证据产物

```text
artifacts/
  evidence/
    manifest.json              # 批次索引、计数、清理状态
    input_manifest.sha256      # data/manifest.json 的哈希绑定
    processing_trace.jsonl     # batch/object/step/exception/boundary/cleanup
    snap-v1.record.json
    unknown-uvx.record.json
    snap-v2.record.json
    boundary_seam.hex          # B-03 原始 16 B + 两个候选指标
    summary.txt
  screenshots/
    main.png                   # snap-v2 检查页
    snap-v1.png
    unknown.png
```

`processing_trace.jsonl` 的每个 `step` 事件都带 `objectId`、`inputSha256`、
`stepId` 和 `evidenceIds`。每个 `record.json` 保存输入路径、SHA-256、
字节数、证据偏移/长度/哈希、处理步骤及结论引用。

## 临时状态与异常

- 每个输入先复制到 `.tmp/unfold-scan-<run>/<id>.staged`，并设为只读。
- `TempWorkspace` 在析构时无条件 `remove_all`；正常结束还会空清理 `.tmp`。
- 清理在证据落盘前完成，`manifest.json` 的 `temporaryState.cleaned` 和
  `processing_trace.jsonl` 的 `temp_cleanup` 记录最终结果。
- 未知扩展异常只影响该对象；后续 `snap-v2` 的完整步骤和证据仍会生成。

## 验证

```bash
ctest --test-dir build --output-on-failure
```

测试覆盖：

- 恰好两个版本快照和一个未知扩展；
- 未知对象被隔离、v2 在异常后继续；
- v2 的 `B-03` 两个候选均在容差内且符号相反；
- 结论及步骤的所有 `Sxx/Exx` 引用无悬空；
- 原始输入 SHA-256 与落盘字节一致；
- `boundary_seam.hex` 含 UNDECIDABLE、E06 指标和输入哈希；
- `.tmp` 已删除，manifest/trace 都报告清理完成；
- 有显示环境时用 Vulkan 渲染一帧并验证 PNG 非空。

## 源码地图

- `src/surface_provenance.*`：SUF 解析、预览、步骤/证据、批处理与产物
- `src/app_ui.*`：检查页布局、证据链与交互
- `src/vulkan_app.*`：GLFW/Vulkan swapchain、ImGui 后端、帧缓冲读回
- `tools/make_fixtures.cpp`：确定性的 v1/v2 快照与未知扩展探针
- `tests/provenance_tests.cpp`：无 GPU 的核心不变量
- `tests/gui_smoke.sh`：可选 Vulkan 截图冒烟测试
