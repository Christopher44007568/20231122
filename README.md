# HDR 纹理格式验证

一个可回溯的 HDR 纹理来源检查页。界面固定使用 **C++17 + Vulkan + GLFW 3.4**
实现，数据与证据链独立于渲染层，可由 CLI 或测试单独验证。

![来源检查页](artifacts/screenshots/main.png)

## 功能

- 记录两个版本快照：`hdr_atlas_v1.hdr`、`hdr_atlas_v2.hdr`。
- 记录一条截断输入：`hdr_atlas_tile_07.partial`（实际 409 B，清单声明 8192 B）。
- 每个结论都绑定 `输入路径 + SHA-256 + 偏移区间 + 处理步骤 Sxx + 证据 Exx`。
- 截断记录触发 `TruncatedRecordError`，对象级 `try/catch` 隔离后继续处理 v2。
- 该边界保持 `UNDECIDABLE`：只确认输入不足，不推断截断发生在采集、写入还是传输。
- EOF 前 24 B 原样写入 `boundary_bytes.hex`，并保留声明长度、实际长度、缺失字节。
- 批次结束时删除 `.tmp/hdr-scan-*`，清单与页面同时报告清理结果。
- HDR 预览纹理来自实际 RGBE 输入解码，不是占位图。

## 构建与运行

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/hdr_provenance
```

常用参数：

```bash
./build/hdr_provenance --verify
./build/hdr_provenance --select tile-07
./build/hdr_provenance --screenshot artifacts/screenshots/main.png --frames 10
```

页面快捷键：`↑/↓` 切换来源对象，`R` 重新验证，`Esc` 退出。
点击截断记录或“原始字节”可查看 E05 的 EOF 关键证据；点击右侧步骤查看该步骤明细。

## 数据与证据

原始输入：

```text
data/manifest.json
data/snapshots/hdr_atlas_v1.hdr
data/snapshots/hdr_atlas_v2.hdr
data/records/hdr_atlas_tile_07.partial
```

运行后写出：

```text
artifacts/evidence/manifest.json
artifacts/evidence/processing_trace.jsonl
artifacts/evidence/boundary_bytes.hex
artifacts/evidence/summary.txt
artifacts/evidence/snap-v1.record.json
artifacts/evidence/tile-07.record.json
artifacts/evidence/snap-v2.record.json
```

`processing_trace.jsonl` 中的每个 `step` 事件都带 `objectId`、`inputSha256`、
`stepId` 和 `evidenceIds`。`boundary_bytes.hex` 记录 E05 的准确偏移和原始字节。

## 批处理语义

1. `S01` 读取原始输入并写入临时只读校验区。
2. `S02` 解析 Radiance 头和分辨率行。
3. `S03` 验证 `32-bit_rle_rgbe`、尺寸和 `headerBytes`。
4. `S04` 解码 scanline。截断记录在这里抛异常。
5. `S05` 正常对象执行版本比较；截断对象记录“异常捕获与隔离”。
6. `S06` 仅对截断对象执行边界判定，结论固定为“无法判断”。
7. 批次结束删除临时工作区，再写证据文件。

## 验证

```bash
ctest --test-dir build --output-on-failure
```

测试覆盖：两快照通过、截断记录不可判定、异常后继续、结论引用无悬空、
E05 边界证据存在、临时目录不存在、清单报告清理完成。

如果运行环境没有 `DISPLAY`，GUI 冒烟测试会以退出码 77 跳过。
