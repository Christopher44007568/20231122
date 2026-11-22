# MSPHERE1 输入格式

所有整数和浮点均为 little-endian。文件由 64 字节头、材质数组、RGBA8 反射球和能量积分数组依次组成。当前版本为 `1`，只接受 `header_size == 64`。

## 文件头

| 偏移 | 类型 | 字段 | 约束 |
|---:|---|---|---|
| 0 | `char[8]` | magic | 固定 `MSPHERE1` |
| 8 | `uint16` | version | 必须为 `1` |
| 10 | `uint16` | header_size | 必须为 `64` |
| 12 | `uint32` | flags | 当前保留，写 `0` |
| 16 | `uint32` | material_count | `1..64` |
| 20 | `uint32` | reflection_width | `1..512` |
| 24 | `uint32` | reflection_height | `1..256` |
| 28 | `uint32` | reserved | 写 `0` |
| 32 | `uint64` | material_offset | 必须 `>= 64` |
| 40 | `uint64` | reflection_offset | 必须 `>= material_offset + material_count*32` |
| 48 | `uint64` | energy_offset | 必须 `>= reflection_offset + width*height*4` |
| 56 | `uint64` | file_size | 必须等于实际文件字节数 |

材质区必须结束在反射球区之前，反射球区必须结束在能量积分区之前。能量积分区最终必须结束于 `file_size`。示例文件采用紧凑布局：

```text
material_offset   = 64
reflection_offset = 64 + material_count * 32
energy_offset     = reflection_offset + width * height * 4
file_size         = energy_offset + material_count * 4
```

## 材质记录

每条记录固定 32 字节：

| 记录内偏移 | 类型 | 字段 | 约束 |
|---:|---|---|---|
| 0 | `float32` | base_color.r | 有限，`[0,1]` |
| 4 | `float32` | base_color.g | 有限，`[0,1]` |
| 8 | `float32` | base_color.b | 有限，`[0,1]` |
| 12 | `float32` | roughness | 有限，`[0,1]` |
| 16 | `float32` | metallic | 有限，`[0,1]` |
| 20 | `float32` | reflectance | 有限，`[0,1]` |
| 24 | `float32` | albedo_scale | 有限，`[0,8]` |
| 28 | `uint32` | flags | 必须为 `0` |

第 `i` 条材质记录在文件中的绝对偏移是：

```text
material_offset + i * 32
```

示例异常目标实例是 `i = 5`，因此 `base_color.r` 的原始偏移为：

```text
64 + 5 * 32 + 0 = 224
```

该字段原始字节为 `1f856b3f`（`0.92f`）。异常帧只把 GPU 上传副本中的这 4 字节替换为 `cdcccc3c`（`0.025f`），不会写回文件。

## 反射球

反射球是行优先、无 padding 的 RGBA8 等距柱状图：

```text
offset = reflection_offset + (y * reflection_width + x) * 4
```

像素字节顺序为 `R, G, B, A`。alpha 当前不参与着色。示例尺寸是 `64x32`，占用 `8192` 字节。

## 能量积分

每个材质一个 `float32`，有限且位于 `[0,4]`：

```text
offset = energy_offset + i * 4
```

着色器把能量积分裁剪到 `[0,1]` 后作为能量增益。报告另外从每个实例的 framebuffer 屏幕单元计算基线/异常平均亮度，给出相对变化。

## 校验与拒绝证据

- 文件大小先于内容读取检查，限制为 `8388608` 字节。
- 空文件：`error_code=empty`，`raw_offset=0`。
- 超大文件：`error_code=too_large`，`raw_offset=8388608`。
- 头截断：偏移等于实际文件大小。
- magic/version/header_size 错误：偏移分别为 `0/8/10`。
- 材质或能量字段错误：偏移指向对应原始字段。

所有拒绝结果保留输入路径、实际大小、错误码、字段名、原始偏移和观测/上限。
