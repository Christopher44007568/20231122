# rtsync — 渲染线程同步证据工具

`rtsync` 把“渲染线程同步”重建为一条可复核流水线：

**接收轨迹 → 时钟归一化 → fence/命令缓冲检查 → 资源冲突扫描 → draw/帧缓冲/画面关联 → 固定顺序导出 → 重新载入 → 独立校验**

技术实现固定为 **C++17 + Vulkan + GLFW**。示例场景模拟两台时钟不同步的设备，
并用真实的 Vulkan 离屏命令缓冲、fence 和读回像素生成 golden/observed 证据。

## 快速开始

依赖已放在 `.toolchain/root/usr`，无需系统安装 Vulkan/GLFW 开发包：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure

# 生成双设备时钟不同步异常场景、Vulkan 画面、证据链和复核表
./build/rtsync demo --out artifacts/demo

# 重新载入 analysis.json，逐字段、逐顺序比较
./build/rtsync reload --artifact artifacts/demo --out artifacts/reload-check

# 独立复核：重算清单、证据链、记录哈希和画面差异
./build/rtsync verify --artifact artifacts/demo
```

示例结果：

- 设备 A 写 `tex-shared` 的归一化区间：`[1100 ms, 1150 ms]`
- 设备 B 读 `tex-shared` 的归一化区间：`[1120 ms, 1170 ms]`
- 原始 tick 看起来有序（`100–150 ms` 早于 `220–270 ms`），但标定后实际重叠 `30 ms`
- fence `f-b` 的 wait 比 signal 早 `30 ms`
- golden/observed 差异为 `8114 / 64000` 像素，包围盒 `(117,28)-(221,118)`
- `analysis.json` 重载后 SHA-256 不变，字段和顺序均保持

## 处理过程

### 1. 输入准入

`analyze` 先检查文件大小，再读取内容。默认上限：

| 限制 | 默认值 | 超限行为 |
|---|---:|---|
| 输入字节 | 32 MiB | 不解析 JSON；保留 64 KiB 前缀、完整文件 SHA-256，状态为 `rejected_oversized` |
| 记录数 | 100000 | 状态为 `rejected_record_limit`，不输出异常结论 |
| findings | 256 | 停止继续收集，并写入 `finding_limit_reached` |

空文件、纯空白文件、`{}` 和零记录轨迹都会生成合法 `empty` 产物，不会崩溃。
非法 JSON 会保留原始输入并生成 `invalid_input` 证据。

### 2. 轨迹解析

输入模式为 `rtsync.trace.v1`，核心数组：

| 字段 | 内容 |
|---|---|
| `devices[]` | 设备 ID、时钟频率、`host_ns/device_ticks` 标定点 |
| `render_threads[]` | 设备 ID、OS thread ID、submit/wait 事件 |
| `command_buffers[]` | record/submit 状态、fence、transition/draw 操作 |
| `fences[]` | signal/wait 原始 host 时间与设备 tick |
| `draw_calls[]` | 命令缓冲、线程、资源、资源状态、设备 tick 区间 |
| `resources[]` | 共享资源的读写访问、各访问的 image layout |
| `framebuffers[]` / `frames[]` | 帧缓冲元数据、golden/observed 图像 |

解析器保留原始 tick 和原始 host 时间，不做就地覆盖。

### 3. 跨设备时钟归一化

每个设备使用至少两个 `(device_ticks, host_ns)` 点做最小二乘拟合：

```text
host_ns = slope_ns_per_tick * device_ticks + intercept_ns
```

报告同时保存原始 `device_ticks`、原始捕获 `host_ns`、拟合斜率/截距/最大残差和
归一化 host 区间。少于两个标定点、斜率非正或残差超过 `1 ms` 会产生显式 finding。

### 4. 同步异常判定

- `fence_wait_before_signal`：归一化后 wait 早于 signal。
- `fence_not_signaled_before_wait`：有 wait 但没有 signal 观测。
- `fence_device_mismatch`：fence 与命令缓冲属于不同逻辑设备。
- `cross_device_resource_race`：不同设备对同一资源的读写区间重叠。
- `cross_thread_resource_race`：同设备不同渲染线程的读写区间重叠。
- `frame_mismatch`：golden 与 observed 帧存在像素差。
- `frame_evidence_unreadable` / `frame_comparison_incomplete`：画面证据不完整。

资源扫描按归一化起点排序，使用活动区间集合，避免无条件两两比较。

### 5. draw / 资源 / 帧缓冲 / 画面对应

每条 finding 生成一条 correlation，包含 `draw_ids`、`resource_ids` 及 writer/reader
state、`fence_ids`、`command_buffer_ids`、`framebuffer_ids`、`frame_ids` 和
`diff_ids`。

golden/observed 帧按 `group` 配对，重新计算变化像素数、最大通道差、平均绝对误差
和变化包围盒。差异图写到 `diffs/<group>.ppm`。

## 产物

```text
artifacts/demo/
  analysis.json          # 固定字段顺序的完整结论
  records.jsonl          # 逐关联记录 + 单条记录 SHA-256
  evidence.jsonl         # 前向 SHA-256 证据链
  manifest.json          # 全部文件的大小与 SHA-256
  manifest.sha256        # manifest 自身摘要，可配合 sha256sum 使用
  REVIEW.md              # 同事可直接阅读的复核表
  input/trace.json       # 完整输入；超限时改为 input/trace.prefix.bin
  frames/*.ppm           # Vulkan 读回的 golden/observed
  diffs/*.ppm            # 重算的差异图
  gpu_probe.json         # 实际 Vulkan 设备、命令缓冲、fence 序列
```

### 固定字段与顺序

所有 JSON 使用插入顺序有序对象写出。`analysis.json` 的顶层顺序固定为：

```text
schema, analyzer, input, status, limits, summary, clock_models,
devices, render_threads, command_buffers, fences, draw_calls,
resources, framebuffers, frames, visual_diffs, findings,
correlations, evidence_chain, exported_records, verification, warnings
```

`reload` 会重新解析、重新序列化，并比较字节是否完全一致、递归字段名顺序是否
完全一致。

### 证据公式

证据链每行哈希输入为 `compact_ordered_json({seq,event,payload,prev_sha256})`；
导出记录哈希输入为 `compact_ordered_json(record_without_record_sha256)`。
`manifest.sha256` 使用标准 `sha256sum` 格式 `<sha256>  manifest.json`。

## 独立复核

```sh
./build/rtsync verify --artifact artifacts/demo
```

校验器不信任报告中的结论，重新执行：

1. `manifest.sha256` 与 `manifest.json`
2. manifest 中每个文件的字节数和 SHA-256
3. 文件集合是否存在遗漏或额外文件
4. 保留输入副本的 SHA-256
5. `analysis.json` 是否仍为规范有序形式
6. `records.jsonl` 每条记录哈希
7. `evidence.jsonl` 的序号、前向哈希和链头
8. 每个帧图像哈希
9. 从 golden/observed 像素重算差异统计、包围盒和差异图 SHA-256

任一失败返回非零，并输出 `checks` 与 `errors` JSON。

## 场景数据

`demo` 构造：

- 设备 A：`host = ticks + 1000 ms`
- 设备 B：`host = ticks + 900 ms`
- A 写资源：ticks `100–150 ms` → host `1100–1150 ms`
- B 读资源：ticks `220–270 ms` → host `1120–1170 ms`
- B 的 fence signal：host `1180 ms`，wait：host `1150 ms`

原始 tick 的直观顺序会掩盖实际重叠，这是异常场景的核心。Vulkan 渲染器使用
真实 command buffer/fence 生成两张离屏帧；`gpu_probe.json` 记录实际物理设备。
场景中的“两台设备”是带独立时钟域的建模输入，UI 不要求机器真的安装两张 GPU。

## 命令行

```text
rtsync demo --out DIR [--max-input-bytes N] [--max-records N] [--max-findings N]
rtsync analyze --input FILE --out DIR [limits]
rtsync reload --artifact DIR [--out DIR]      # 默认 DIR.reload
rtsync verify --artifact DIR [--report FILE]
```

退出码：`0` 成功或合法空输入，`1` 运行失败，`2` 输入非法/被限制。

## 代码结构

- `src/clock.cpp`：标定点最小二乘、tick→host_ns 映射、区间重叠
- `src/trace.cpp`：轨迹解析、记录计数、引用去重警告
- `src/analyzer.cpp`：fence、资源冲突、帧差异和 correlation
- `src/report.cpp`：固定顺序 JSON、records、manifest 和 Markdown
- `src/artifact.cpp`：准入、超大/空输入、证据链、重载和独立复核
- `src/renderer.cpp`：GLFW 隐藏窗口 + Vulkan 离屏渲染、fence、读回
- `src/scenario.cpp`：双设备不同步时钟异常场景
- `tests/`：单元测试与空/超大输入进程级测试

## 已知边界

- PPM 用于无额外图像库依赖的像素证据；可用任意支持 PPM 的查看器检查。
- 演示渲染器要求可用的 Vulkan ICD；当前环境使用 lavapipe。
- 实际双物理 GPU 共享资源通常需要外部内存/跨设备同步；本工具验证的是轨迹输入、
  时钟归一化、同步证据和导出复核流程，场景中的共享资源按跨设备逻辑资源建模。

