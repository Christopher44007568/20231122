# 处理过程

## 1. 输入与原始材料保护

`load_and_validate` 先用 `file_size` 做空输入和 8 MiB 上限检查，再读取并校验区域偏移、字段范围和保留位。通过后，`preserve_original` 将内存中的完整字节流写到证据目录，重新计算落盘文件哈希，并在渲染结束后再次计算源文件哈希。两次哈希相同才通过 `original.byte_identical`。

原始材料有两份：

- `materials/original.material`：固定示例源
- `evidence/acceptance/original/original.material`：本次运行写出的逐字节副本

异常不修改这两份文件，只修改 CPU 构建的 `GpuMaterial` 上传数组。

## 2. Vulkan 资源图

| 资源 | 内容 | 用途 |
|---|---|---|
| vertex buffer | 32x64 段 UV 球，位置+法线 | 重复网格 |
| index buffer | `12288` 个 `uint16` 索引 | 索引绘制 |
| instance buffer | 8 条 `{position, material_index}` | `gl_InstanceIndex` 取实例 |
| material buffer | 8 条 32 字节 GPU 材质 | 颜色、粗糙度、金属度、反射率、能量 |
| reflection image | 64x32 `R8G8B8A8_UNORM` | fragment 采样等距柱状反射 |
| color image | 960x540 `R8G8B8A8_UNORM` | 两帧共用 color attachment/readback |
| depth image | 960x540 `D32_SFLOAT` | 球体深度 |
| framebuffer | color + depth view | 两帧共用 |
| readback buffer | 960*540*4 字节 | 每帧 RGBA 原始回读 |
| timestamp query pool | 2 个 64-bit timestamp | 每帧 top/bottom 时间戳 |

两帧各发出一次 `vkCmdDrawIndexed(indexCount=12288, instanceCount=8, firstIndex=0, vertexOffset=0, firstInstance=0)`。

## 3. 帧内状态转换

基线和异常帧使用同一 color image/framebuffer，显式同步：

```text
UNDEFINED
  -> COLOR_ATTACHMENT_OPTIMAL
  -> render instanced spheres
  -> TRANSFER_SRC_OPTIMAL
  -> vkCmdCopyImageToBuffer
  -> COLOR_ATTACHMENT_OPTIMAL
```

深度图首次从 `UNDEFINED` 转到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`，之后保持该布局。反射球上传路径是：

```text
UNDEFINED -> TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
```

命令缓冲在写入 timestamp 和 draw 后提交，fence 等待完成，再读取 query pool 与 coherent readback buffer。

## 4. 异常与关联链

异常目标是实例 `5`，其网格实例位置仍在 `first_instance=0` 的同一 draw 中。基线材质缓冲和异常材质缓冲只有一条 32 字节记录中的 4 字节不同。证据关联如下：

```text
anomaly.instance_index       = 5
draw.first_instance          = 0
relative_instance            = 5
source material record       = 224
source field base_color.r    = 224
GPU material buffer offset   = 160
baseline material SHA-256    != anomaly material SHA-256
first differing raw RGBA     = 293324
difference bounds            = (286,76)-(462,252)
instance screen cell         = (283,73)-(467,257)
```

`report.json` 的 `correlation` 对象直接保存上述关联，`draw_calls` 保存 framebuffer/color/depth/material/instance 句柄，`resources.images` 保存布局转换，`framebuffer` 产物保存精确回读字节。

## 5. 能量对照

每个实例在世界坐标中有固定屏幕单元。`evaluate_energy` 对单元内非背景像素计算平均 RGB 亮度：

```text
luma = (0.2126*R + 0.7152*G + 0.0722*B) / 255
relative_change = (anomaly_luma - baseline_luma) / baseline_luma
```

`energy_comparison` 同时记录输入能量积分、基线亮度、异常亮度和相对变化。示例目标实例的通过阈值是 `|relative_change| >= 0.05`。

## 6. 空输入与超大输入

两类失败都在读取/分配前终止，并生成独立 JSON：

- `empty_input.json`：`code=empty`，`raw_offset=0`，观测大小 `0`
- `oversized_input.json`：先创建大小为 `8388609` 的文件，`code=too_large`，首个拒绝字节 `8388608`

自测同时将这些状态写入总报告的 `invalid_inputs` 和验收检查。
