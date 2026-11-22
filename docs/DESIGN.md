# 设计说明

## 场景与极限参数

世界由 `8 × 8` 个瓦片组成，每瓦片边长为 512 个世界单位。缺失片段固定为
`[28, 36)`，对应八块不生成植被的瓦片。瓦片 27 是缺失片段前最后一块，瓦片 36
是缺失片段后第一块。

`Params::extreme()` 定义了一组用于压测的参数：

| 字段 | 值 | 作用 |
| --- | ---: | --- |
| `seed` | `0x20231122D15EA5E5` | 跨运行、跨 CPU/Vulkan 的随机源 |
| `densityMilli` | 8800 | 高密度，计入瓦片目标实例数 |
| `jitterMilli` | 4200 | 纵轴抖动范围 |
| `altitudeMilli` | 72000 | 基准海拔 |
| `slopeBiasMilli` | 900 | 坡度造成的纵轴偏置 |
| `windMilli` | 120000 | 水平风偏移强度 |
| `boundaryBoostMilli` | 1750 | 瓦片 27/36 的边界密度增强 |
| `maxInstancesPerTile` | 4096 | 未降级容量上限 |
| `missingStart/End` | 28 / 36 | 缺失片段半开区间 |

生成器先用整数哈希确定目标计数：

```text
base   = 240 + hash(tile, seed, tag_count) % 720
extra  = densityMilli * 232 / 1000
boost  = boundaryBoostMilli * 180 / 1000
count  = min(capacity, base + extra + boost)
```

缺失瓦片直接得到计数 0 和空哈希。其余瓦片逐实例生成位置与物种编号，位置使用
12 位定点随机数、整数风偏和整数坡度计算，最后一次性乘 `0.001` 转为浮点。这样
CPU 与 Vulkan 不会因 FMA 或表达式结合顺序产生末位差异。

## 暂停点与跨进程状态

默认暂停参数为 `--pause-at 28`，即完成瓦片 0–27 后停止。暂停点位于缺失片段
入口，`nextTile` 归一化为 36，恢复进程明确从缺失片段之后继续。

检查点采用小端二进制格式：

```text
magic "VEGST001"
version / reserved
全部 Params
nextTile
effectiveCapacity
budgetBytes
partial aggregateHash
completedMask (64 bit)
tileCount
counts[64]
tileHashes[64]
for completed tile: instanceCount + Instance[instanceCount]
FNV-1a checksum (uint64)
```

写入先落到 `.tmp`，成功后原子重命名。加载时检查 magic、版本、长度、计数合法
性、尾随字节和校验和。恢复流程不会重新生成已完成瓦片，而是直接加载其计数、
哈希和实例字节。

暂停和恢复是两个独立的 `vegdist` 进程。暂停进程退出后不保留任何 GPU 或 CPU
对象；恢复进程只依赖检查点。

## 资源不足与降级

实例池预算按如下方式换算为每瓦片容量：

```text
usable      = max(0, budgetBytes - 64 KiB)
rawCapacity = usable / (64 tiles * 16 bytes)
capacity    = clamp(rawCapacity, 64, maxInstancesPerTile)
```

若容量小于请求上限，结果和证据记录
`instance-capacity-clamped:<capacity>`。若预算连最低容量都不足，还会记录
`budget-exhausted:minimum-capacity-64`。恢复时优先采用检查点中的
`effectiveCapacity`，避免后半程与前半程使用不同上限。

后端选择顺序：

1. 尝试 Vulkan 计算设备；
2. Vulkan 初始化失败时记录 `vulkan-unavailable:<原因>;cpu-fallback`；
3. `--force-cpu` 时记录 `cpu-forced;cpu-fallback`；
4. 已初始化 Vulkan 但单瓦片提交失败时，记录 `vulkan-tile-failed:<原因>` 并切换
   到 CPU。

CPU 降级不是伪实现，它复用相同的整数哈希、计数规则、定点坐标和瓦片哈希。因此
同一参数下的 Vulkan 与 CPU 聚合哈希相同，降级不会改变最终分布。

## 边界样本与比较

每个结果保存两个样本：

- `beforeBoundary`：瓦片 27 的接受计数、首实例位置和瓦片哈希；
- `afterBoundary`：瓦片 36 的接受计数、首实例位置和瓦片哈希。

暂停结果只有前置边界样本；恢复结果同时保存加载时与最终的前置样本，并给出
`beforeStable`。比较文件额外给出：

```json
{
  "boundaryDelta": {
    "beforeAtPause": {},
    "beforeFinal": {},
    "afterFinal": {},
    "beforeChanged": false,
    "afterChanged": false
  }
}
```

`compare` 的判定字段是聚合 `bufferHash`、`paramHash`、总计数、有效容量、逐瓦片
计数和两个边界样本。没有差异时写 `verdict: "identical"`；任何差异会写入
`diffs` 并以退出码 2 结束。后端名称/设备不同只写 `provenanceChanged`，不单独
判为数据差异，因为 CPU 与 Vulkan 必须生成相同分布。

## 证据链

`artifacts/evidence.jsonl` 每行一个事件，包含时间戳与事件字段。关键事件：

- `glfw-init`：GLFW 版本和 null/窗口模式；
- `backend` / `backend-ready`：Vulkan 设备、容量和降级列表；
- `params` / `missing-segment` / `resource-plan`：完整输入；
- `pause` / `state-saved`：检查点路径、字节数、校验和、`nextTile`；
- `resume` / `state-loaded`：加载校验和与容量；
- `resume-boundary`：暂停前与恢复后的边界样本及稳定性；
- `boundary-samples`：全量运行的两个边界样本；
- `compare`：最终哈希、差异和判定。

`artifacts/degraded/evidence.jsonl` 单独保留低预算、强制 CPU 和 Vulkan 不可用
路径。`artifacts/SHA256SUMS` 对所有关键 JSON、检查点和证据文件做内容哈希。
