# 像素历史采样器

一个固定使用 **C++17 + Vulkan + GLFW** 的可暂停像素历史采样示例。场景由 4 个相同的方块沿同一圆周运动；8 个固定像素探针位于该圆周上，因此每个像素会周期性地看到重复对象的不同实例。

两台设备使用不同步的时钟渲染同一场景：

- 设备 A：`a_raw = truth + a_epoch`
- 设备 B：`b_raw = b_epoch + round((truth + b_phase) * (1 + skew_ppm / 1e6))`
- 未校准：`b_scene = b_raw - b_epoch`
- 校准后：`b_calibrated = round((b_raw - b_epoch) / (1 + skew_ppm / 1e6)) - b_phase`

产物同时保存原始时钟、场景时钟、校准时钟、每帧 SHA-256、每个探针的 RGBA/对象 ID/模型期望值，以及检查点最后帧的完整证据。

## 构建

依赖：

- CMake 3.20+
- 支持 C++17 的编译器
- Vulkan loader 与开发头文件
- GLFW 3.3+

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

SPIR-V 已作为 `src/shaders_spv.h` 随仓库提供，因此普通构建不需要 `glslc`。修改 `shaders/` 后可重新生成：

```bash
PATH=/path/to/glslang/bin:$PATH python3 tools/embed_spirv.py
```

## 运行

```bash
# 连续运行 + 暂停检查点 + 恢复 + 前后/连续对照（默认）
./build/pixel_history --mode both --out artifacts/demo

# 只生成暂停检查点
./build/pixel_history --mode paused --out artifacts/pause --frames 12 --pause-after 5

# 从检查点继续，并将源检查点复制到输出目录保证自包含
./build/pixel_history --mode resume --in artifacts/pause/checkpoint.json \
  --out artifacts/pause-final

# 独立复核任意 run/checkpoint/failure 产物
./build/pixel_history --mode verify --in artifacts/demo/run-paused.json \
  --out artifacts/demo

# GLFW 窗口 + Vulkan 离屏渲染/交换链预览
./build/pixel_history --mode interactive
```

交互模式：`Space` 暂停/继续，`R` 重新开始，`Esc` 退出。

## 暂停与恢复

`--pause-after N` 表示完成第 N 个采样步骤后暂停。检查点 `checkpoint.json` 保存：

- 两台设备的全部已完成帧，按 `device_a, device_b` 和步骤升序排列；
- 下一处理步骤、资源计划、时钟参数和场景参数；
- 最后帧 SHA-256、完整探针证据和总流摘要；
- 规范化 UTF-8 JSON、字段顺序声明与产物 SHA-256。

恢复流程先从磁盘重新解析检查点，校验字段顺序、字节级回写和 SHA-256；然后重新渲染暂停帧的两台设备，逐探针比较 RGBA、对象 ID 和帧摘要。只有该检查通过才继续后续帧。`before_after` 同时保存暂停前摘要与恢复后摘要。

`--mode both` 还会运行一条不间断的连续流，并比较两条完整数据流的摘要。两份结果必须逐帧一致。

## 资源不足与降级

运行前按 `--memory-budget-kb` 和 `--max-quality` 选择质量档位：

| 档位 | 分辨率 | 保留完整历史帧 |
| --- | ---: | ---: |
| `high` | 320x200 | 8 |
| `balanced` | 240x150 | 6 |
| `low` | 160x100 | 4 |
| `minimal` | 96x60 | 2 |
| `emergency` | 64x40 | 1 |

预算不足时逐级降采样；每一步都记录 `from_quality`、`to_quality`、原因、估算字节数和预算。Vulkan 初始化或运行时失败也会触发下一档重试。最低档仍超预算时不会崩溃，而是写出 `failure.json`、返回码 `3`，其中包含完整降级路径。

示例：

```bash
./build/pixel_history --mode both --memory-budget-kb 600 --out artifacts/degraded
./build/pixel_history --mode continuous --memory-budget-kb 8 --out artifacts/blocked
```

## 产物

| 文件 | 内容 |
| --- | --- |
| `run-continuous.json` | 不间断参考流 |
| `checkpoint.json` | 暂停时中间状态与关键证据 |
| `run-paused.json` | 恢复完成流、恢复校验和连续对照 |
| `run-continuous.review.json` | 连续产物复核报告 |
| `run-paused.review.json` | 暂停/恢复产物复核报告 |
| `failure.json` | 资源阻塞证据 |
| `verify-report.json` | `--mode verify` 报告 |

所有记录使用有序 JSON。顶层和关键嵌套对象的字段顺序声明在 `field_order` 中；加载时必须与声明完全一致。`artifact_digest` 是移除自身后对整个 compact JSON 计算的 SHA-256。

仓库内已包含三组可直接复核的证据：

- `artifacts/demo/`：`high` 档连续运行、暂停、恢复和连续对照；
- `artifacts/degraded/`：预算不足后降到 `minimal` 的完整降级路径；
- `artifacts/blocked/failure.json`：最低档仍超预算时的阻塞证据。

## 同事独立复核

复核不需要运行图形窗口：

```bash
./build/pixel_history --mode verify --in artifacts/demo/run-paused.json \
  --out artifacts/demo
```

复核器会独立完成以下检查：

- UTF-8 JSON 解析后回写是否逐字节一致，根/嵌套字段顺序是否保持；
- 产物 SHA-256、设备顺序、帧顺序、步号与字段数量；
- 用 `clock_model.formulas` 重算每帧的原始/场景/校准时钟；
- 用文档化场景模型重算每个探针的 RGBA 与对象 ID；
- 重算检查点摘要、暂停前缀和恢复后摘要；
- 重新加载同目录检查点，逐帧比较暂停前缀；
- 加载同目录 `run-continuous.json`，比较完整数据流摘要。

任一检查失败时返回码为 `1`，报告仍会完整写出。

## 测试

```bash
ctest --test-dir build --output-on-failure
```

CTest 会运行默认双路径流程并复核 `run-paused.json`。
