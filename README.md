# 程序化植被分布：可暂停 / 可恢复流程

这是一个固定采用 **C++17 + Vulkan + GLFW** 的植被分布演示。流程把 8×8 瓦片
作为处理单元，在缺失片段 `[28, 36)` 前暂停，原子保存已完成瓦片的实例数据与
哈希，再由新的进程恢复处理。全量运行与恢复运行会逐瓦片比较，并记录缺失片段
前后两个边界样本。

## 核心行为

- **极限参数场景**：高密度 `8.8`、高抖动 `4.2`、强风 `0.12`、边界密度增强
  `1.75`，缺失片段两侧的瓦片 27 和 36 额外增强。
- **缺失片段**：瓦片 28–35 不生成实例；暂停点记录 `nextTile = 36`，恢复时
  从缺失片段之后继续。
- **中间状态**：`artifacts/checkpoint.vstate` 保存参数、随机种子、已完成位图、
  每瓦片计数、每瓦片哈希和全部已完成实例；文件尾带 FNV-1a 校验和。
- **跨进程恢复**：`pause` 和 `resume` 是两次独立启动，不共享内存；恢复时先
  校验检查点，再沿用保存时的有效容量。
- **结果比较**：`compare` 比较聚合哈希、总实例数、逐瓦片计数和两个边界样本。
  正常情况下结论为 `identical`。
- **资源不足**：`--budget-kb 256` 通过容量上限模型把每瓦片容量限制为 192，并
  在结果和证据中记录 `instance-capacity-clamped:192`；恢复运行仍完全一致。
- **降级路径**：Vulkan 不可用、强制 CPU 或单瓦片计算失败时切换到确定性的 CPU
  实现。CPU 与 Vulkan 使用同一整数哈希和定点坐标算法，聚合哈希一致。
- **关键证据**：JSONL 记录 GLFW、Vulkan 设备、参数、资源计划、暂停、状态校验和、
  恢复、边界样本、降级原因和最终比较。

## 构建与验证

工程自带 Vulkan 头文件、GLFW 头文件和 GLFW 运行库，不要求系统安装开发包。

```sh
./scripts/build.sh
./scripts/run_validation.sh
```

`run_validation.sh` 会重新生成所有交付 artifacts，并执行 CTest：

- `core`：生成、边界、检查点往返、损坏检查点拒绝、低容量恢复一致性；
- `resume_flow`：真实进程级全量 / 暂停 / 恢复 / 对比，低预算和 CPU 降级。

也可以手动执行：

```sh
./build/vegdist --mode full   --out artifacts/result_full.json \
  --evidence artifacts/evidence.jsonl --reset-evidence
./build/vegdist --mode pause  --out artifacts/result_paused.json \
  --state artifacts/checkpoint.vstate --evidence artifacts/evidence.jsonl
./build/vegdist --mode resume --out artifacts/result_resumed.json \
  --state artifacts/checkpoint.vstate --evidence artifacts/evidence.jsonl
./build/vegdist --mode compare --a artifacts/result_full.json \
  --b artifacts/result_resumed.json \
  --out artifacts/compare_full_resumed.json \
  --evidence artifacts/evidence.jsonl
```

常用参数：

| 参数 | 说明 |
| --- | --- |
| `--budget-kb N` | 设置实例池预算，触发容量截断 |
| `--force-cpu` | 强制走 CPU 降级路径 |
| `--pause-at N` | 处理到瓦片 `N-1` 后暂停 |
| `--ui` | 打开 GLFW 状态窗口，逐瓦片显示进度 |
| `--reset-evidence` | 本轮开始前截断证据文件 |

## 结果文件

| 文件 | 内容 |
| --- | --- |
| `artifacts/result_full.json` | 无中断全量结果 |
| `artifacts/result_paused.json` | 暂停点计数、边界样本和状态摘要 |
| `artifacts/result_resumed.json` | 恢复后的最终结果 |
| `artifacts/compare_full_resumed.json` | 全量与恢复结果的逐项比较 |
| `artifacts/checkpoint.vstate` | 可跨进程加载的二进制中间状态 |
| `artifacts/evidence.jsonl` | 关键事件证据链 |
| `artifacts/degraded/` | 低预算、强制 CPU、Vulkan 不可用三类降级证据 |
| `artifacts/SHA256SUMS` | 交付证据文件的内容哈希 |

正常情况下 `compare_full_resumed.json` 包含：

```json
{
  "verdict": "identical",
  "diffs": [],
  "boundaryDelta": {
    "beforeChanged": false,
    "afterChanged": false
  }
}
```

## 目录

- `src/vegetation.*`：参数、确定性生成、聚合与边界哈希；
- `src/state.*`：检查点序列化、原子写入和损坏检测；
- `src/vulkan_backend.*`：Vulkan 计算设备、存储缓冲、调度和回读；
- `src/pipeline.*`：Vulkan / CPU 选择和降级；
- `src/app.cpp`：全量、暂停、恢复、比较四个模式；
- `shaders/vegetation.comp` 与 `.spv`：计算着色器源码和预编译 SPIR-V；
- `docs/DESIGN.md`：状态格式、边界样本与降级规则。

## 运行说明

无窗口模式使用 GLFW 的 `null` 平台进行真实初始化，因此可在无桌面环境运行；
`--ui` 使用 GLFW 窗口显示瓦片进度。Vulkan 计算设备优先选择独立 GPU，其次集成
GPU、CPU 设备；本环境使用 `llvmpipe` 软件 Vulkan 设备。
