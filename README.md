# 景深散景形状可暂停处理流水线

这是一个可复现的 C++17 + Vulkan + GLFW 实验。它围绕同一个重复对象，对两台存在
时钟偏移和漂移的捕获设备做双探针仿射标定；先跑不间断基线，再在任务边界保存检查点，
由全新流水线实例重新载入并继续，最后逐帧比较两次结果。

核心处理实际使用 Vulkan compute shader。资源不足时自动切换为分带 Vulkan 或 CPU
参考路径，降级原因和资源事件会写入证据。GLFW 用于可选的 Vulkan 交换链预览窗口。

## 构建与运行

依赖：

- CMake 3.20+
- 支持 C++17 的编译器
- Vulkan loader、头文件和可用的 ICD
- GLFW 3.3+

Ubuntu 环境可安装：

```sh
sudo apt install build-essential cmake libvulkan-dev mesa-vulkan-drivers libglfw3-dev
```

构建并运行默认实验：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bokeh-pipeline --out artifacts/demo
```

默认生成 12 个序列、每序列 2 个设备观测任务，共 24 帧；在第 7 个任务后暂停。

常用参数：

```sh
./build/bokeh-pipeline \
  --out artifacts/demo \
  --width 480 --height 300 \
  --frames 10 \
  --pause-after 7 \
  --memory-mb 24 \
  --backend auto \
  --preview
```

`--backend` 可取 `auto`、`vulkan` 或 `cpu`。`--preview` 会在复核通过后用 GLFW
创建窗口，通过 Vulkan swapchain 显示恢复后的最终帧；按 Esc 或关闭窗口退出。

## 流水线

1. 基线实例处理全部 24 个观测任务。
2. 暂停实例处理前 7 个任务，把已完成帧、顺序、哈希、下一任务索引、双设备标定、
   渲染计划和资源事件写入 `paused/checkpoint.json`。
3. 恢复实例重新读取检查点，校验字段顺序及前缀帧哈希，在 `next_task_index=7`
   处继续处理，而不是重跑前缀。
4. `comparison.json` 比较基线序列和恢复序列，报告逐帧 SHA-256、最大通道差、
   平均绝对误差和最终帧哈希。
5. 两个额外压力探针分别触发 `vulkan-banded` 和 `cpu-reference` 降级路径，
   证明资源不足时仍能继续并留下证据。

两台逻辑设备为 `cam-alpha` 和 `cam-beta`。两者使用各自的设备本地时间戳；检查点
和清单中保存原始时间、标定探针、估计参考时间与残差。渲染前统一映射到参考时钟，
避免时钟不同步直接污染对象时序。九个六边形光圈盘属于同一种重复对象模板。

## 产物

```text
artifacts/demo/
  baseline/                不间断基线
  paused/                  暂停前缀、检查点、暂停时最终帧
  resumed/                 恢复后的完整序列
  pressure-banded/         低显存预算下的分带 Vulkan 探针
  pressure-cpu/            连一行显存都不足时的 CPU 参考探针
  comparison.json          基线/恢复逐帧对比
  evidence.json            哈希索引、时钟证据、检查项、降级事件
  review.md                同事可读的复核摘要
```

每个帧清单记录 `task_index`、序列、设备、估计参考时间、相对路径、SHA-256 和字节数。
`evidence.json` 对除自身以外的产物逐文件登记大小和 SHA-256。

## 独立复核

同事不需要 GPU，也不需要重新运行渲染：

```sh
python3 tools/review_artifact.py artifacts/demo
```

复核器只使用 Python 标准库，会检查：

- 固定顶层字段和字段顺序
- 检查点、清单、对比、证据中的产物哈希和大小
- PPM 尺寸与实际像素字节数
- 暂停前缀是否在恢复输出中按原任务号和原顺序保留
- 基线与恢复帧的逐像素字节相等性
- 两台设备的偏移/漂移、重复对象标识
- 分带 Vulkan 与 CPU 降级路径及资源事件

也可以使用内置检查器：

```sh
./build/bokeh-pipeline verify artifacts/demo
./build/bokeh-pipeline inspect artifacts/demo
```

要从落盘检查点启动一个全新进程继续：

```sh
./build/bokeh-pipeline resume \
  --checkpoint artifacts/demo/paused/checkpoint.json \
  --out artifacts/demo-resumed
```

该命令会重新校验检查点字段顺序、时钟模型和前 7 帧哈希，只处理剩余任务。CTest 会把
它与内置恢复结果做字节级比较。

## 字段与顺序

所有 JSON 由有序对象写入器生成，不从无序映射回写。重新载入检查点时，下列顶层字段
必须严格匹配，顺序不同会直接拒绝恢复：

```text
schema, checkpoint_id, created_at_utc, config, scene, device_clocks,
render_plan, progress, frames, resource_events, state, field_order
```

帧记录固定为：

```text
task_index, sequence, device_index, device_id, estimated_reference_ns,
file, sha256, bytes
```

检查点的 `frames` 数组按 `task_index` 连续排列。恢复实例先校验每个前缀文件哈希，
再按原数组顺序复制到 `resumed/frames/`，随后追加剩余任务。字段约定同时记录在每个
文档的 `field_order` 数组中。

## 资源降级

完整瞬时缓冲估算为 `width * height * 4 * 3` 字节。规划规则：

| 条件 | 路径 | 证据 |
| --- | --- | --- |
| 预算足够 | `vulkan-full` | 单次 Vulkan dispatch |
| 预算不足一行 | `cpu-reference` | `gpu_capacity_degraded` 事件 |
| 预算介于两者 | `vulkan-banded` | 分带高度、带数、容量事件 |
| Vulkan 初始化失败 | `cpu-reference` | `vulkan_init_failed` 和 VkResult 文本 |
| Vulkan 运行期失败 | CPU 重试 | `compute_dispatch_failed` 事件 |

素材路径和命令都写入 `evidence.json`，因此复核者可区分“结果真的相同”和
“两边都走了同一条降级路径”。

## 测试与着色器

```sh
ctest --test-dir build --output-on-failure
```

CTest 覆盖默认 Vulkan 运行、显式 CPU 运行、内置 verify、inspect 和 Python 独立复核。

SPIR-V 已随仓库保存，普通构建不要求安装 `glslangValidator`。修改 shader 后重新生成：

```sh
glslangValidator -V --target-env vulkan1.1 shaders/bokeh.comp -o shaders/bokeh.comp.spv
glslangValidator -V --target-env vulkan1.1 shaders/preview.vert -o shaders/preview.vert.spv
glslangValidator -V --target-env vulkan1.1 shaders/preview.frag -o shaders/preview.frag.spv
```

