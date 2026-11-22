# skincheck

可暂停的“动画蒙皮权重检查”示例。工程固定使用 **C++17**；图形查看器使用 **Vulkan + GLFW**，检查、暂停恢复、证据和复核逻辑是无图形依赖的确定性核心，可在 CI 中运行。

## 快速开始

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/skincheck demo --out artifacts/demo
./build/skincheck verify --run artifacts/demo/final
```

图形查看器需要 `libvulkan-dev` 与 `libglfw3-dev`。若构建时找不到它们，CMake 会生成 CLI 版本而不是构建失败：

```bash
sudo apt install libvulkan-dev libglfw3-dev
cmake -S . -B build -DSKINCHECK_BUILD_GUI=ON
cmake --build build -j
./build/skincheck gui --run artifacts/demo/final
```

查看器用颜色条显示每帧状态：绿色 `pass`、蓝色 `pass_degraded`、红色 `fail`、黄色 `incomplete`、橙色 `fail_incomplete`。按 `Esc` 退出。

## 演示场景

`demo` 生成两台设备的动画捕获：

- `device-a` 时钟偏移 `500 us`，漂移 `0 ppm`，覆盖全部 24 帧。
- `device-b` 时钟偏移 `38000 us`，漂移 `4000 ppm`，缺少第 `8..11` 帧。
- 时钟投影后，缺失片段对应参考时间 `[80000, 120000) us`。
- `device-b` 相邻记录的本地间隔为 `50200 us`，标称间隔为 `50000 us`，因此时钟偏移影响为 `200 us`；边界不确定度为 `5000 us`。

程序在第 8 帧暂停并保存检查点。恢复时先重算已处理的 `0..7` 帧，逐帧比较 `result_sha256`，再继续 `8..23` 帧。演示结果为 `8/8` 帧匹配、无字段漂移；缺失片段仍标记为 `incomplete`，不会伪造权重或给出虚假通过。

内置问题用于验证证据链：权重和错误、负权重、超出影响数上限、重复骨骼索引。完整模式下共 5 条问题；降级模式会明确跳过 `duplicate_bone` 检查并显示 `pass_degraded`。

## 命令

```bash
# 生成捕获，暂停、恢复并自动复核
skincheck demo [--out DIR] [--degraded]

# 检查到指定帧后保存 checkpoint
skincheck run --capture FILE --out DIR [--pause-at N]
              [--memory-budget-bytes N | --memory-budget-kib N]

# 从 checkpoint 独立恢复；先重算旧帧再继续
skincheck resume --checkpoint DIR/checkpoint.json --out DIR

# 同事复核：读取产物、重算每帧、校验字段顺序与 SHA-256
skincheck verify --run DIR

# Vulkan/GLFW 状态视图
skincheck gui --run DIR
```

`run` 未提供 `--pause-at` 时，默认暂停在检测到的第一个缺失片段起点。

## 捕获输入

```json
{
  "schema": "skincheck.capture/v1",
  "asset_id": "rig",
  "frame_period_us": 10000,
  "devices": [
    {
      "device_id": "device-a",
      "clock": {"offset_us": 500, "drift_ppm": 0.0, "uncertainty_us": 50},
      "frames": [
        {
          "sequence": 0,
          "local_time_us": 500,
          "vertices": [
            {"vertex_id": 0, "weights": [{"bone_id": 1, "value": 1.0}]}
          ]
        }
      ]
    }
  ]
}
```

参考时间按 `reference_us = (local_time_us - offset_us) / (1 + drift_ppm / 1e6)` 投影，再以 `frame_period_us` 取最近参考槽位。两台设备可独立设置时钟；连续缺失槽位达到 `min_gap_frames` 时记为缺失片段。

## 暂停与恢复语义

`checkpoint.json` 内嵌完整捕获、策略、资源计划、时钟投影、缺失片段、已处理帧及摘要，因此不依赖进程内状态或绝对路径。恢复流程：

1. 校验 checkpoint 的 `capture_sha256`。
2. 从捕获和时钟元数据重建时间线。
3. 用同一资源策略重算所有已完成帧。
4. 比较 checkpoint 与重算结果的 `result_sha256`。
5. 从 `next_frame` 继续，最后输出新增帧数、匹配数和不一致数。

若旧帧摘要不一致，最终记录仍会保留 `stable=false`、差异帧和两侧摘要，进程返回非零退出码。

## 资源不足与降级

资源计划估算保留完整证据所需的内存。超过预算且允许降级时：

- 模式切换为 `cpu_streaming_degraded`。
- 跳过 `duplicate_bone` 检查；该检查不会静默执行。
- 无问题的帧标记为 `pass_degraded`，并写入 `resource_degradation` 证据。
- 运行记录包含预算、完整模式估算值、原因和 `skipped_checks`。

例如：

```bash
./build/skincheck demo --out artifacts/demo-degraded --degraded
```

若禁用降级且预算仍不足，任务会失败，而不是无证据地降低检查标准。

## 导出产物

每个运行目录包含：

- `capture.json`：规范化输入捕获。
- `checkpoint.json`：自包含中间状态；恢复和复核均可重新载入。
- `frames.jsonl`：按帧号严格递增的检查结果与证据哈希。
- `evidence.jsonl`：时钟投影、缺失片段、资源路径、检查点、蒙皮问题和恢复比较。
- `review.md`：同事可直接阅读的摘要、因果说明、问题表和哈希表。
- `run.json`：有序运行清单、阶段摘要、资源/时间线证据及产物索引。
- `SHA256SUMS`：所有关键文件的标准校验清单。
- `verification.json`：执行 `verify` 后生成的独立复核报告。

`run.json` 的 `record_order` 和各类 JSONL 记录的字段顺序是稳定契约。JSON 解析器保留对象字段插入顺序；复核器会把 `run.json`、`checkpoint.json`、`capture.json` 重新序列化并要求字节级一致，同时检查 `frames.jsonl` 的字段顺序与帧号顺序。

## 独立复核

同事只需运行：

```bash
skincheck verify --run artifacts/demo/final
sha256sum -c artifacts/demo/final/SHA256SUMS
```

复核器不会信任清单中的摘要，而会：

1. 从 `capture.json` 和时钟元数据独立重建时间线及缺失片段。
2. 用记录中的策略重建资源计划。
3. 重新执行每帧蒙皮检查，比对 `input_sha256`、`result_sha256` 和全部字段。
4. 验证检查点中的旧帧与重算结果一致。
5. 检查所有产物哈希及 `SHA256SUMS`。

## 代码结构

- `src/core.cpp`：双设备时钟投影、缺口检测、权重检查、资源降级、恢复比较。
- `src/persist.cpp`：有序 JSON、检查点、产物导出、复核器、哈希清单。
- `src/gui.cpp`：Vulkan 实例/交换链/渲染通道和 GLFW 窗口，逐帧状态可视化。
- `src/main.cpp`：`demo/run/resume/verify/gui` 命令。
- `tests/test_main.cpp`：顺序保持、时钟缺口、暂停比较、降级、导出/重载和篡改检测。
