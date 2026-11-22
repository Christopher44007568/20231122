# lshc_review - 光源阴影缓存复核器

围绕两个版本的缓存快照生成可复核证据：逐对象执行 Vulkan 离屏渲染，把
draw call、Vulkan 资源状态、帧缓冲内容和画面差异写入同一条 JSONL 记录。
工具同时接受旧版固定记录格式和当前格式，并在遇到截断记录或单对象渲染异常
时保留现场、继续处理其余对象。

固定技术栈：**C++17 / Vulkan / GLFW**。

## 快速开始

```sh
cmake -S . -B build
cmake --build build -j
./run_validation.sh
```

默认会在 `fixtures/` 生成两组确定性快照，并把完整证据写到
`artifacts/shadow-review/`：

```sh
./build/lshc_review \
  --legacy fixtures/snapshot-v1.4.2-legacy.cache \
  --current fixtures/snapshot-v2.0.0-current.cache \
  --out artifacts/shadow-review
```

无参数运行也会在缺少默认 fixture 时自动生成，然后执行同一条审查流程。
需要软件 Vulkan 时：

```sh
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
./build/lshc_review --out artifacts/shadow-review
```

## 复核输出

```text
artifacts/shadow-review/
  manifest.json                      运行、快照、截断、异常、清理总览
  cleanup.json                       临时目录与 Vulkan 资源清理结果
  records.jsonl                      每个对象一行，关联四类证据
  review.md                          人类可读的对照表和异常说明
  checksums.sha256                   所有证据文件的 SHA-256
  frames/object-0001-legacy.ppm       旧快照帧缓冲
  frames/object-0001-current.ppm      新快照帧缓冲
  frames/object-0001-diff.ppm         实际像素差异（增强显示）
  snapshots/legacy.cache              原始旧快照副本
  snapshots/current.cache             原始新快照副本
  snapshots/current-truncated-tail.bin 截断记录的原始尾部字节
```

`records.jsonl` 的核心结构：

```json
{
  "object_id": 2,
  "status": "compared",
  "legacy_record": {"draw_call": 2, "raw_offset": 64},
  "current_record": {"draw_call": 2, "raw_offset": 88},
  "draw_call": {"legacy": 2, "current": 2, "stable": true},
  "renders": {
    "legacy": {
      "draw_call": {"pipeline": "...", "descriptor_set": "..."},
      "resources": {"uniform_buffer": {}, "color_target": {}},
      "framebuffer": {"framebuffer": "...", "content_sha256": "..."}
    },
    "current": {}
  },
  "visual_diff": {"changed_pixels": 2708, "bounds": {}},
  "evidence": {
    "legacy_frame": "frames/object-0002-legacy.ppm",
    "current_frame": "frames/object-0002-current.ppm",
    "diff_frame": "frames/object-0002-diff.ppm"
  }
}
```

每个渲染侧都记录：

- draw call：实际 `VkCommandBuffer`、pipeline、pipeline layout、descriptor set、
  图元拓扑和顶点/实例参数。
- 资源状态：uniform buffer 和颜色目标的句柄、内存字节数、格式、布局转换、
  写入 stage/access、内容 CRC-32C 与 SHA-256。
- 帧缓冲：`VkRenderPass`、`VkFramebuffer`、颜色视图、extent、格式、clear
  color 与读回内容的 SHA-256。
- 画面差异：变化像素数、占比、平均绝对差、RMSE、最大通道差和变化包围盒。

只有缓存参数变化才会造成画面变化。旧/新格式标记和 pipeline id 不参与着色，
因此格式迁移本身不会产生伪差异。

## 缓存格式兼容

两种快照使用相同 magic `LSHC`，由 header 中的 `version` 分派解析。

旧格式 `version=1`：

```text
Header 16 bytes: magic[4], u16 version, u16 headerSize=16,
                 u32 recordCount, u32 recordStride=48
Record 48 bytes: objectId, drawCall, centerX/Y, radius, lightX/Y/Z,
                 strength, penumbra, resourceRevision, crc32c
```

当前格式 `version=2`：

```text
Header 24 bytes: magic[4], u16 version, u16 headerSize=24,
                 u32 recordCount, u32 recordStride=64,
                 u64 snapshotId
Record 64 bytes: 前 48 字节与旧格式兼容，随后增加
                 pipelineId, flags, contentHash
```

两种格式的前 44 字节都受 CRC-32C 保护；当前格式额外用 FNV-1a 64 校验前 56
字节。解析器按 header 中的 stride 读取完整记录，不依赖文件长度推断版本。

示例当前快照声明 6 条记录，实际只有 5 条完整记录和 32 字节尾部。工具会：

1. 解析全部 5 条完整记录；
2. 安全提取截断尾部的 object id 和 draw call（若对应字节存在）；
3. 将 32 字节原文写入 `snapshots/current-truncated-tail.bin`；
4. 在 `manifest.json` 记录 offset、期望长度、可用长度和原文 hex；
5. 仍为旧快照中的 object 6 渲染基线帧。

## 异常与清理

对象循环对每个 side 单独使用异常边界。示例新快照的 object 4 具有非有限
半径，校验失败后只把 object 4 标为 `failed`，object 5 继续完成比较。
截断记录同样不会终止剩余对象。

每次运行会在输出目录内创建 `.lshc-tmp-*`。正常退出前删除该目录，并在
`manifest.json` 的 `cleanup` 中记录：

- `temp_removed=true`
- `vulkan_shutdown=true`
- `live_transient_objects=0`

`--keep-temp` 仅用于调试现场，会在总览中明确标记未清理。

## 命令

```text
--legacy PATH       旧格式快照
--current PATH      当前格式快照
--out DIR           证据目录，默认 artifacts/shadow-review
--fixtures DIR      fixture 目录，默认 fixtures
--make-fixtures     生成确定性旧/新格式样本后退出
--self-test         运行解析、Vulkan、差异、异常继续和清理自检
--width/--height    离屏帧尺寸，默认 256x256
--keep-temp         保留临时目录
--verbose           打印逐对象进度
```

## 验证

```sh
ctest --test-dir build --output-on-failure
(cd artifacts/shadow-review && sha256sum -c checksums.sha256)
```

依赖通过 CMake 查找；仓库自带 Vulkan 头文件、GLFW 共享库和预编译 SPIR-V，
因此没有系统开发包时仍可构建。存在 `glslangValidator` 时会优先从 GLSL 源码
重新编译着色器。
