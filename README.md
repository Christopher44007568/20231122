# 屏幕空间描边重建与异常关联

一个固定采用 **C++17 + Vulkan + GLFW** 的离屏诊断工具。它把一次屏幕空间描边
处理拆成 G-buffer、边缘度量、最终合成三个 Vulkan pass，并对重复对象造成的
描边异常逐项关联：draw call、资源布局、framebuffer、GPU 时间戳、读回图像和
最终画面差异。

工作区保留了原始种子材料（`materials/original/`，只读），包括整合环境写入的
`workspace.json` 和初始 `README`；验收会校验字节级 SHA-256。

## 构建

依赖已放在 `third_party/`：Vulkan 头文件与 loader、GLFW 3.4（Null 平台）、
`nlohmann/json` 和预编译 SPIR-V。构建不需要 shader 编译器。

```bash
./scripts/build.sh
# 或
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

运行环境需要 Vulkan loader 与一个 ICD；本工作区默认使用 lavapipe：

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/ssor run
```

若要重新编译 GLSL：

```bash
GLSLANG_VALIDATOR=/path/to/glslangValidator ./scripts/compile_shaders.sh
```

脚本会重新生成 `shaders/spv/*.spv` 和嵌入头
`src/generated/shaders_spv.h`。当前嵌入版本由 glslang 16.2.0 生成。

## 运行

```bash
# 单次重建：baseline（单对象）与 repeat（重复对象）两帧
./build/ssor run --material materials/raw/outline_repeat.ssoraw \
  --out artifacts/run-$(date +%s)

# 只读检查格式和原始偏移
./build/ssor inspect --material materials/raw/outline_repeat.ssoraw

# 全部验收：正常场景、空输入、空边界、超大文件、超大计数、边界种子救援
./build/ssor accept --out artifacts/acceptance
```

`run` 输出目录中的关键文件：

```text
run.json                  完整材料、场景、draw call、资源、framebuffer、图像摘要
evidence.jsonl            SHA-256 前向哈希链，逐事件记录 UTC/epoch/单调时钟
chain_head.json           链头序号和哈希
draw_calls.json           两场景的逐 draw call 证据
resource_states.json      逐次 image layout transition
framebuffers.json         每个 pass 的附件格式/尺寸/clear/layout
boundary_report.json      每个边界像素的源偏移、度量、命中和 outline 状态
correlation.json          画面差异、对象 ID 归因和重复对象结论
diff/final_diff.ppm       baseline 与 repeat 的差异图
boundary/overlay.ppm      边界像素评估叠加图
frames/<scene>/*.ppm      normal/depth、metrics、boundary、final
original/                 输入、workspace.json、README.seed.md 的只读副本
SHA256SUMS                以上关键文件的内容哈希
```

## 输入材料

`materials/raw/outline_repeat.ssoraw` 是自定义小端二进制 `SSORAW1`：

| 字节偏移 | 字段 | 当前值 |
|---:|---|---|
| 0 | magic `SSORAW1\0` | — |
| 8 | version | 1 |
| 12 | headerBytes | 80 |
| 16 / 20 | width / height | 320 / 240 |
| 24 | objectCount | 3 |
| 28 | boundaryCount | 35 |
| 32 | `outlineDepth`（深度差阈值） | 0.035 |
| 36 | `normalThreshold`（法线差阈值） | 0.30 |
| 40 / 44 | boundary GT eps | 0.004 / 0.02 |
| 48 / 60 | camera eye / target | — |
| 80 | objects[0] (`ground`) | 80 字节/对象 |
| 160 | objects[1] (`pillar.base`) | — |
| 240 | objects[2] (`pillar.repeat`) | duplicate, `repeatOf=1` |
| 284 | `objects[2].originalOffset` | `0.58, 0, 0.06` |
| 320 | boundaries[0] | 16 字节/像素 |

`objects[2]` 与 `objects[1]` 使用同一个 cube 网格、同一个 G-buffer pipeline，
但位置偏移不同。repeat 场景因此出现 3 个对象 draw call，baseline 只有 2 个；
两者重叠处产生重复描边，并在最终画面中形成可见 seam。

边界像素记录为 `x,y,expected,objectIndex`，`expected` 为
`background / boundary / interior`。其中 `boundary` 类像素会写入 GPU 存储
缓冲，并在 edge shader 中 `OR` 进最终 mask；它们不是仅供展示的标注。

## 三遍处理

1. **G-buffer**：每个对象一次 `vkCmdDrawIndexed`，输出视空间法线/深度
   （`R32G32B32A32_SFLOAT`）和对象 ID（`R32_UINT`）。`firstInstance` 就是
   对象 ID，证据中可直接对应。
2. **Edge**：全屏三角形读取 G-buffer，计算四邻域最大相对深度差、最大法线差、
   二阶 `1/z` 边界强度；`mask = depthDiff > outlineDepth ||
   normalDiff > normalThreshold`，再与边界种子缓冲按位或。
3. **Final**：全屏三角形把法线着色、几何边界（青色）和最终描边（红色）
   合成到 `R8G8B8A8_UNORM`。

每个 pass 前后都有显式 image layout barrier，并记录旧/新 layout、stage、
access 和命令序号。每个 pass 另有 Vulkan timestamp query；lavapipe 上
`timestampValid=true`，报告中给出 tick 差和纳秒值。

## 重复对象关联

`repeat` 场景的 G-buffer draw call：

| Draw | Object | firstInstance | Mesh SHA-256 | Pipeline |
|---:|---|---:|---|---|
| 1 | ground | 1 | plane mesh | `PIPE_gbuffer` |
| 2 | pillar.base | 2 | cube mesh | `PIPE_gbuffer` |
| 3 | pillar.repeat | 3 | 与 draw 2 相同 | `PIPE_gbuffer` |

关联流程：

1. 比较 baseline 与 repeat 的 final 图像，得到差异像素数、bbox、质心、
   平均/最大通道误差。
2. 在 repeat 的 object-ID 图像中读取每个差异像素的 3x3 邻域，把差异归因到
   具体对象 ID 和对应 draw call。
3. 在 repeat 的 edge metrics 中统计“差异像素且 mask 命中且邻域含 duplicate ID”
   的 seam 像素。
4. 校验 duplicate draw 与 base draw 共享 mesh buffer hash、pipeline 和
   pipeline layout；输出
   `REPEATED_OBJECT_OUTLINE_ANOMALY` 或 `NO_DUPLICATE_ANOMALY`。

当前样例：baseline 4 个 draw（2 对象 + edge + final），repeat 5 个 draw
（3 对象 + edge + final）；差异 2431 像素，全部归因到 `pillar.repeat`，
其中 588 像素同时命中重复描边 mask。

## 空输入与超大输入

| 情况 | 退出码 | 证据 |
|---|---:|---|
| 0 字节文件 | 4 | `EMPTY_INPUT`, offset 0, bytesRead 0 |
| 8 MiB+1 文件 | 5 | `OVERSIZED_FILE`, observed 8388609, limit 8388608, bytesRead 0 |
| `boundaryCount=0xFFFFFFFF` | 5 | `OVERSIZED_DECLARED_COUNT`, offset 28, observed 4294967295, limit 65536 |
| 合法材料但 `boundaryCount=0` | 0 | 正常渲染，warning `EMPTY_BOUNDARY_SET` |

超大文件在 `stat` 后直接拒绝，不读取全部内容；只读取 64 字节用于
`inputPrefixSha256` 指纹。超大计数在分配 `vector` 前按文件字节数和字段上限
双重检查。

## 验收

```bash
./scripts/run_acceptance.sh
```

`ssor accept` 会真实执行 6 个 case，并写入：

```text
artifacts/acceptance/
  acceptance_report.json     机器可读结论、原始材料、偏移、case、checks
  acceptance_report.md       人工复核表：时间点、原始偏移、draw 关联、负例
  evidence_timeline.csv      所有 case 的事件时间线与链哈希
  checks.json                逐项检查及检查时刻
  cases/canonical/           完整重建证据（含 PPM、run.json、哈希链）
  cases/empty/               0 字节拒绝证据
  cases/empty_boundaries/    空边界集合证据
  cases/oversized_file/      8 MiB+1 拒绝证据
  cases/oversized_count/     count 溢出拒绝证据
  cases/seed_rescue/         关闭全部阈值后仅靠边界种子恢复描边的证据
  SHA256SUMS                 验收目录内容哈希
```

验收必须全部通过：

- 原始材料、只读副本、run 内副本 SHA-256 一致；`workspace.json` 和初始
  README 种子字节不变。
- `outlineDepth=32`、`normalThreshold=36`、`objectCount=24`、
  `boundaryCount=28`、`objects=80`、`boundaries=320`、
  duplicate `originalOffset=284`。
- baseline/repeat draw call 数量、共享 mesh/pipeline、3 个 framebuffer
  附件形状、layout transition 均存在。
- 差异像素 > 50，duplicate 归因 > 0，duplicate seam > 0。
- 35 个边界像素全部评估，首个源偏移为 320。
- 每个证据链逐事件 hash 连续，链头匹配。
- 每个 draw、pass、case 都有微秒级 UTC 时间；lavapipe 上 GPU timestamp
  必须有效。
- 空输入、空边界、超大文件、超大计数和 seed-rescue 均按上述语义处理。

## 原始材料保留

以下文件权限为 `0444`，且不会在运行中被修改：

```text
materials/raw/outline_repeat.ssoraw      可重新生成的输入材料
materials/raw/empty_boundaries.ssoraw    boundaryCount=0 输入
materials/raw/empty.ssoraw               0 字节输入
materials/original/                      上述文件的只读副本
materials/original/workspace.json        整合环境原始文件
materials/original/README.seed.md        初始 README 原文
```

每次 `run` 还会在输出目录 `original/` 下再复制一份输入与两个种子文件，
并记录 SHA-256、字节数和权限位。

## 源码地图

- `src/core/material.*`：`SSORAW1` 读写、边界检查、逐字段源偏移
- `src/core/generator.*`：规范材料、凸包投影边界像素
- `src/core/scene.*`：cube/plane、相机、baseline/repeat 场景
- `src/core/sha256.*`、`src/core/timestamp.*`、`src/core/util.*`：证据基础
- `src/render/vk_context.*`：GLFW Null 平台、Vulkan device、buffer/image
- `src/render/renderer.*`：三 pass、timestamp query、readback、图像哈希
- `src/evidence.*`：差异/归因/边界评估、JSON、SHA-256 前向链
- `src/runner.*`：单次运行、原始材料快照、失败证据、SHA256SUMS
- `src/accept.*`：6 个 case、时间点/偏移验收、Markdown 报告
- `tests/test_core.cpp`：SHA-256、格式偏移、重复对象、空/超大输入回归
