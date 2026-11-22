# 材质球能量对照验收

本文件由 `--self-test` 生成，所有时间点均为实际运行观测值，所有偏移均指向保留的原始字节流。

- 运行开始：`2026-11-22T02:57:08.514033Z`
- 运行结束：`2026-11-22T02:57:08.645628Z`
- 总耗时：`131595 us`
- 原始材料：`materials/original.material`
- 未修改副本：`evidence/acceptance/original/original.material`
- SHA-256（运行前）：`a861013f86ebd36bee88aecda9e1fe9aa1a33c50c841e032303c3ec249d996b8`
- SHA-256（运行后）：`a861013f86ebd36bee88aecda9e1fe9aa1a33c50c841e032303c3ec249d996b8`
- 原始字节未改动：`true`

## 时间点

| 事件 | UTC 时间 | 相对耗时 (us) | 说明 |
|---|---|---:|---|
| `run_start` | `2026-11-22T02:57:08.514054Z` | 21 | self-test process entered |
| `input_validated` | `2026-11-22T02:57:08.515492Z` | 1462 | materials=8, bytes=8544 |
| `original_preserved` | `2026-11-22T02:57:08.515693Z` | 1662 | byte-for-byte copy at evidence/acceptance/original/original.material |
| `empty_input_checked` | `2026-11-22T02:57:08.515717Z` | 1685 | code=empty, raw_offset=0 |
| `oversized_input_checked` | `2026-11-22T02:57:08.515747Z` | 1715 | code=too_large, raw_offset=8388608 |
| `render_start` | `2026-11-22T02:57:08.515748Z` | 1716 | GLFW null platform + Vulkan offscreen |
| `baseline_rendered` | `2026-11-22T02:57:08.570943Z` | 56915 | gpu=12766501.000000 ns |
| `anomaly_rendered` | `2026-11-22T02:57:08.570949Z` | 56917 | gpu=4805155.000000 ns |
| `framebuffers_written` | `2026-11-22T02:57:08.645183Z` | 131179 | raw RGBA + PNG + amplified difference |
| `checks_evaluated` | `2026-11-22T02:57:08.645396Z` | 131364 | 12/12 passed |
| `report_written` | `2026-11-22T02:57:08.645623Z` | 131591 | evidence/acceptance/report.json |
| `acceptance_written` | `2026-11-22T02:57:08.645624Z` | 131591 | evidence/acceptance/acceptance.md |
| `run_end` | `2026-11-22T02:57:08.645625Z` | 131592 | all evidence captured and serialized |

## 原始偏移

| 字段 | 原始偏移 (byte) | 观测值 |
|---|---:|---|
| header.magic | 0 | `MSPH...` |
| material_offset | 64 | 8 条 32-byte 材质记录 |
| reflection_offset | 320 | 64x32 RGBA8 |
| energy_offset | 8512 | 8 个 float32 能量积分 |
| anomaly.field | 224 | `base_color.r` 原始字节 `1f856b3f` -> `cdcccc3c` |
| baseline.rgba first diff | 293324 | pixel 73331 |

异常实例 `5` 对应原始材质记录偏移 `224`，字段偏移再增加 0 字节；注入仅发生在 GPU 上传副本，原始文件未写入。

## Draw call 与帧缓冲

| 帧 | first_instance | instance_count | index_count | GPU start tick | GPU end tick | GPU ns |
|---|---:|---:|---:|---:|---:|---:|
| `baseline` | 0 | 8 | 12288 | 35160582661204 | 35160595427705 | 12766501.000 |
| `anomaly` | 0 | 8 | 12288 | 35160597459751 | 35160602264906 | 4805155.000 |

两帧共享同一 color image/framebuffer；布局按 `UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL` 转换，异常帧结束后再次回读。

关联链：异常帧 draw sequence `2`，`first_instance=0` 中的实例 `5`；原始材质记录偏移 `224`，GPU 材质缓冲偏移 `160`，framebuffer `0x00005d36c34b59c0` 的首差异 RGBA 偏移 `293324`，差异包围盒 `(286,76)-(462,252)`，实例屏幕单元 `(283,73)-(467,257)`。

## 画面差异

- 差异像素：`24519`
- 差异字节：`24519`
- 最大通道差：`85`
- 平均通道差：`0.686`
- 差异包围盒：`(286, 76) - (462, 252)`
- 首差异像素：`73331`，RGBA 原始偏移：`293324`

## 能量对照

| 实例 | 输入能量积分 | 基线平均亮度 | 异常平均亮度 | 相对变化 | 屏幕单元 |
|---:|---:|---:|---:|---:|---|
| 0 | 0.72 | 0.445 | 0.445 | 0 | (72,283)-(256,467) |
| 1 | 0.81 | 0.531 | 0.531 | 0 | (283,283)-(467,467) |
| 2 | 0.64 | 0.526 | 0.526 | 0 | (493,283)-(677,467) |
| 3 | 0.88 | 0.535 | 0.535 | 0 | (704,283)-(888,467) |
| 4 | 0.69 | 0.426 | 0.426 | 0 | (72,73)-(256,257) |
| 5 | 0.84 | 0.494 | 0.446 | -0.0978 | (283,73)-(467,257) |
| 6 | 0.77 | 0.587 | 0.587 | 0 | (493,73)-(677,257) |
| 7 | 0.58 | 0.409 | 0.409 | 0 | (704,73)-(888,257) |

## 空输入与超大输入

| 用例 | UTC 时间 | 文件大小 | 上限 | 错误码 | 原始偏移 | 观测/上限 |
|---|---|---:|---:|---|---:|---|
| `empty_input` | `2026-11-22T02:57:08.515701Z` | 0 | 8388608 | `empty` | 0 | 0/64 |
| `oversized_input` | `2026-11-22T02:57:08.515726Z` | 8388609 | 8388608 | `too_large` | 8388608 | 8388609/8388608 |

## 验收检查

| 检查项 | 结果 | 期望 | 实际 |
|---|---|---|---|
| `input.valid` | PASS | `valid MSPHERE1 package` | `valid` |
| `original.byte_identical` | PASS | `source hash before == after` | `a861013f86ebd36bee88aecda9e1fe9aa1a33c50c841e032303c3ec249d996b8 == a861013f86ebd36bee88aecda9e1fe9aa1a33c50c841e032303c3ec249d996b8` |
| `input.offsets` | PASS | `material=64, reflection=mat_end, energy=reflection_end, file_size=energy_end` | `material=64, reflection=320, energy=8512, file_size=8544` |
| `draw_calls.repeated_object` | PASS | `two instanced draw calls, first_instance=0, instance_count=8` | `baseline=8, anomaly=8` |
| `gpu.timestamps` | PASS | `both frames have non-zero timestamp query results` | `baseline=12766501.000000 ns, anomaly=4805155.000000 ns` |
| `framebuffer.extent` | PASS | `960x540 RGBA8 readback` | `960x540 RGBA8 readback` |
| `image.difference` | PASS | `>1000 differing pixels` | `24519 pixels` |
| `image.difference.localized` | PASS | `all differences inside anomaly instance screen cell` | `inside=24519, outside=0` |
| `energy.target_change` | PASS | `|relative change| >= 0.05` | `-0.097829` |
| `empty_input.rejected` | PASS | `empty, raw_offset=0` | `empty, raw_offset=0` |
| `oversized_input.rejected` | PASS | `too_large at first rejected byte 8388608` | `too_large, raw_offset=8388608, observed=8388609` |
| `resource.reflection_layout` | PASS | `reflection sphere uploaded to SHADER_READ_ONLY_OPTIMAL` | `SHADER_READ_ONLY_OPTIMAL` |

## 关键产物

- `framebuffer.baseline.raw`: `evidence/acceptance/frames/baseline.rgba` (2073600 bytes, SHA-256 `8ebd051ecd375cdbc716a532d600c5f6c1c20c009b1f82876f2d35c530cf367f`)
- `framebuffer.anomaly.raw`: `evidence/acceptance/frames/anomaly.rgba` (2073600 bytes, SHA-256 `c21fb6242e8b5fe26090f8267696ef2e8691a21b4493d62f32b761f934d590f6`)
- `framebuffer.baseline.png`: `evidence/acceptance/frames/baseline.png` (1555923 bytes, SHA-256 `d0fc81e29704f9f742cb838820b07d5e21e3c4895053010734419579cbc3bff6`)
- `framebuffer.anomaly.png`: `evidence/acceptance/frames/anomaly.png` (1555923 bytes, SHA-256 `05f3fdbd10644dfcd6d7f508b9d70612acab48ea82f7fdac33223f1f0c50820f`)
- `framebuffer.difference.png`: `evidence/acceptance/frames/difference.png` (1555923 bytes, SHA-256 `06e41182dd44d14517b2ff0c27a8b268191005239ab86fd97aede398eb182a84`)
- `original.unmodified`: `evidence/acceptance/original/original.material` (8544 bytes, SHA-256 `a861013f86ebd36bee88aecda9e1fe9aa1a33c50c841e032303c3ec249d996b8`)
- `original.sha256`: `evidence/acceptance/original.sha256` (84 bytes, SHA-256 `1d22d587f601b874b6ecf762e0749c28ecf5eeabec9f75eba2efca4f92e3aeb0`)
- `invalid.empty_input.json`: `evidence/acceptance/empty_input.json` (390 bytes, SHA-256 `30bac00203221d4be40988c4533fc1508171631a0ddd811b2234d906540cc094`)
- `invalid.oversized_input.json`: `evidence/acceptance/oversized_input.json` (450 bytes, SHA-256 `b356dbf009d98ef29489b0782b784486def9c8fd28897311dfe44695873a8701`)
