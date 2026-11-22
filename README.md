# GBuffer Inspector

一个面向延迟渲染的局部修正与 GBuffer 取证工具。它把原始场景冻结为只读材料，
用三个固定锚点组织“原件、对象隔离、修正后验收”，并让每个渲染对象都能单独
重跑、比较、导出和验签。

技术栈固定为 **C++17 + Vulkan 1.1 + GLFW 3**。Vulkan 头文件和 GLFW 3.4
源码已随仓库固定，构建不依赖网络；运行时使用系统 Vulkan loader。

## 三段锚点

| 锚点 | 名称 | 语义 |
|---|---|---|
| `A0` | source | 未修正原件的全场景 GBuffer，同时保存每个对象的隔离视图 |
| `A1` | isolate | 指定对象应用局部 overlay 后的单独重跑，不重画其他对象 |
| `A2` | accept | 应用全部 overlay 后的全场景重组，供最终验收 |

`A0/A2` 会生成 `scene` 视图和每个对象的隔离视图；`A1` 必须指定
`--object`，只生成该对象。所有视图包含：

即使 workspace 中已有其他对象的 overlay，重新捕获 `A0` 也会强制忽略全部
overlay，保证 A0 始终是可比较的原件基准。

- `albedo`：`R8G8B8A8_UNORM`
- `normal`：`R8G8B8A8_UNORM`
- `position`：`R16G16B16A16_SFLOAT`
- `object_id`：`R32_UINT`
- `depth`：`D32_SFLOAT`
- `final`：延迟光照后的 `R8G8B8A8_UNORM`

## 关键保证

- 原始材料只读：`assets/original_scene.json` 带固定 SHA-256；导入后是
  `<workspace>/originals/scene.json`，权限 `0444`。修正只写
  `state/overlays/<object>.json`。
- 每次捕获前后都校验原件。原件哈希、大小或权限变化时拒绝捕获。
- 每个视图有独立原始文件、字节长度、SHA-256、附件绝对偏移和探针。
- 每次动作写入 `state/journal.jsonl` 并 `fsync`；任务先写 staging，再原子
  提升到 `captures/<anchor>/<capture_id>/`。
- 中断会保留 job 文件、staging 原始视图和 `INTERRUPTED.txt`，下次执行
  `resume` 可继续。
- `verify` 会重算原件、capture manifest、视图 raw、探针字节和导出清单哈希。

## 构建

需要 CMake 3.20+、支持 C++17 的编译器、Ninja 或 Make，以及系统
`libvulkan.so.1`。如果系统有 GLFW 开发包会优先使用；否则使用仓库内
GLFW 3.4 构建。没有 X11/Wayland 开发头时，自动构建 GLFW null 平台，
CLI 和离屏 Vulkan 捕获仍完整可用，`run` 窗口模式不可用。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

需要软件 Vulkan 时，可显式选择 Mesa lavapipe：

```bash
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
```

有 Vulkan/显示环境时，可加跑完整端到端测试：

```bash
cmake -S . -B build -DGBI_ENABLE_E2E_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# 或直接：
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./tests/e2e.sh ./build/gbuffer-inspector
```

## 快速开始

```bash
BIN=./build/gbuffer-inspector
W=./gbuffer-workspace

$BIN init --workspace "$W"
$BIN capture --workspace "$W" --anchor A0 --note original

$BIN correct --workspace "$W" --object cube \
  --albedo '#3D8CEB' --roughness 0.32
$BIN capture --workspace "$W" --anchor A1 --object cube \
  --revision cube-fix --note isolated

$BIN capture --workspace "$W" --anchor A2 \
  --revision accept-all --note full-scene-acceptance

$BIN compare --workspace "$W" --object cube --from A0 --to A1
$BIN export  --workspace "$W" --object cube --from A0 --to A2
$BIN verify  --workspace "$W"
```

一键生成三个对象各自的隔离比较与导出：

```bash
$BIN demo --workspace ./demo-workspace --force
```

## 命令

```text
init     [--workspace DIR] [--force]
status   [--workspace DIR]
anchors  [--workspace DIR]
correct  --object ID [--albedo #RRGGBB|r,g,b] [--roughness 0..1]
         [--metallic 0..1] [--clear]
capture  --anchor A0|A1|A2 [--object ID] [--revision LABEL] [--note TEXT]
         [--width 640] [--height 360]
compare  [--object ID] [--from A0] [--to A1|A2]
         [--left-capture ID] [--right-capture ID]
export   [--object ID] [--from A0] [--to A1|A2] [--out DIR]
verify   [--workspace DIR]
resume   [--workspace DIR]
demo     [--workspace DIR] [--force]
run      [--workspace DIR]
```

`compare` 和 `export` 不传 `--object` 时使用会话中选中的对象。`A1` 的
latest 索引按对象分别记录，因此 `panel`、`prism` 的比较不会误用 `cube`
的隔离结果。

## 窗口模式

```bash
$BIN run --workspace "$W"
```

快捷键：

| 按键 | 动作 |
|---|---|
| `1`/`2`/`3` | 选择对象 |
| `Tab` | 下一个对象 |
| `F` | 在对象隔离视图和全场景视图之间切换 |
| `F5` | 捕获 `A0` |
| `F6` | 捕获当前对象 `A1` |
| `F7` | 捕获 `A2` |
| `E` | 导出 `A0 -> A1` |
| `Shift+E` | 导出 `A0 -> A2` |
| `S` | 运行验签 |
| `Q` / `Esc` | 保存会话并退出 |

窗口统一使用 Vulkan 离屏 GBuffer，再 blit 到 swapchain；没有另走 OpenGL。
关闭窗口、Ctrl+C、SIGTERM 都会写 `clean_exit` 和 selected object，重新
进入时沿用会话与 latest 锚点。

## 证据与偏移

`capture.json` 是权威 manifest，典型结构：

```text
captures/<anchor>/<capture_id>/
  capture.json
  SHA256SUMS
  scene.gbuffer.bin
  objects/<object>.gbuffer.bin
```

单个视图 raw 文件由六个附件按 256 字节对齐连续存放，附件顺序固定为
`albedo, normal, position, object_id, depth, final`。manifest 中每个附件记录：

```json
{
  "name": "albedo",
  "format": "R8G8B8A8_UNORM",
  "width": 640,
  "height": 360,
  "pixel_stride": 4,
  "row_stride": 2560,
  "offset": 0,
  "length": 921600,
  "sha256": "..."
}
```

- **raw offset**：从该视图 raw 文件第 0 字节起的绝对偏移。
- **probe offset**：`attachment.offset + y * row_stride + x * pixel_stride`；
  manifest 保存坐标、绝对偏移和实际字节 hex，`verify` 会重新读取比对。
- **source offset**：`originals/scene.json` 中对象 JSON 对象的起始字节和长度。
- **overlay offset**：overlay 文件中被修改字段值的起始字节和长度。
- **view timestamp**：`rendered_at.utc` 与 `rendered_at.unix_ms`；
  capture 级别另有 `started_at` / `completed_at` 和 monotonic ns。

验收文档 `acceptance.md` 会直接列出左右锚点的 capture、UTC、Unix ms、
raw SHA-256、附件左右偏移、变更像素边界和逐探针原始字节。例如仓库内样例：

- `examples/cube-A0-to-A2-acceptance.md`
- `examples/cube-A0-to-A2-scene-evidence.md`
- `examples/cube-A0-to-A2-diff.png`

样例中的源对象偏移在 `originals/scene.json` 的 `offset=366`，GBuffer
albedo 附件偏移为 `0`，首个探针绝对偏移为 `98892`；对应时间点和哈希均在文档中。

## 导出包

`export` 生成自包含目录：

```text
accept-<timestamp>-<object>-<from>-to-<to>/
  manifest.json
  acceptance.md
  scene-evidence.md
  comparison.json
  SHA256SUMS
  raw/{left,right}/objects/<object>.gbuffer.bin
  scenes/{left,right}/scene.gbuffer.bin
  scenes/{left,right,diff}/*.ppm
  views/{left,right}/*.ppm
  diff/{albedo,final}.ppm
  originals/scene.json
  overlays/<object>.json
  evidence/{left,right}-capture.json
  evidence/{left,right}-SHA256SUMS
  evidence/journal.jsonl
```

`scene-evidence.md` 单独记录全场景视图的 UTC、Unix ms、raw 哈希和附件偏移。
导出中原件副本保持只读。`SHA256SUMS` 覆盖除自身外的全部文件。

## 中断与恢复

捕获流程按阶段写 job：

```text
state/jobs/<job_id>.job
captures/.staging-<job_id>/
```

正常提交时 staging 目录通过 `rename` 原子移动到
`captures/<anchor>/<capture_id>/`。若进程在创建完整 manifest 前退出：

1. `INTERRUPTED.txt` 记录阶段、UTC、Unix ms 和错误；
2. 已渲染的 raw 视图不删除；
3. job 保留为 `interrupted_after_crash`；
4. 重新进入后 `resume` 使用原 `capture_id` 重新捕获并完成提交；
5. 旧的 staging 目录作为关键证据保留。

自动化测试用 `GBI_TEST_ABORT_AFTER_VIEW=N` 注入可捕获中断，用
`GBI_TEST_CRASH_AFTER_VIEW=N` 注入直接 `_Exit(137)` 的硬退出；两个环境变量
仅用于测试，不改变正常路径。

## 验签

`verify` 检查：

- 原件大小、SHA-256、只读权限和 source asset 副本；
- capture 的 scene/original 哈希；
- 每个 `SHA256SUMS` 条目；
- 每个 raw 视图的长度与 SHA-256；
- 全部附件的 256 字节对齐、长度、row stride；
- 全部探针坐标公式、绝对偏移与实际字节；
- 对象 source range 是否仍落在原件内并包含对象 ID；
- overlay 哈希；
- 所有 export 目录的 `SHA256SUMS`；
- 未完成 job 会作为 warning 保留，不会静默删除。

报告写入 `state/verification.json`，失败返回码为 `4`。

## 目录

```text
assets/                  冻结原件与 SHA-256
include/gbi/             公共数据模型
src/                     Vulkan、会话、比较、导出、GLFW GUI
shaders/                 GLSL 与固定 SPIR-V
third_party/             固定版本 Vulkan Headers / GLFW 3.4
tests/                   原始材料检查与端到端验收
examples/                实际生成的验收样例
```

## 说明

- 原始场景是三个不同形状与材质的对象，便于观察对象 ID、法线、位置和深度。
- 本工具只做局部材质 overlay，不修改原始 transform 或几何。
- PPM 是刻意选择的零依赖导出格式；可用图像工具转 PNG。
- 当前窗口尺寸固定为 1280x720，GBuffer 固定为 640x360；CLI 可用
  `--width/--height` 覆盖捕获尺寸。
