# 法线贴图切线校验（NMTS）可暂停处理流程

固定技术栈：**C++17 + Vulkan 计算管线 + GLFW**。围绕 5 组极限参数、每组
8 个重复对象（共 40 个实例）组织批量校验，支持分块提交、检查点暂停/恢复、
资源不足降级，并把所有关键证据落到可审计的文件中。

## 极限参数与重复对象

材料由 `nmts_verify generate` 确定性生成（5 个用例 × 8 次重复）：

| case_id | 名称 | 注入的极限条件 | 预期失败位 |
|---|---|---|---|
| 0 | canonical_valid_green_flip | 合法帧 + 偶数重复翻转绿色通道 | 无（全部通过） |
| 1 | non_orthogonal_tangent_30deg | T 向 B 倾斜 30° | ortho, normal-length |
| 2 | non_unit_tangent_scaled | 切线长度 1.25→3.0 | tangent-unit |
| 3 | inverted_handedness | B 取负，左手系 | handedness |
| 4 | degenerate_parallel_frame | T/B 平行、法线 z≤0、green-flip | ortho, handedness, degenerate, normal-length, normal-z, green-flip |

7 个校验位的并集在每次运行中都被覆盖（`fail_mask` 并集 = `0x7f`）。

## 二进制材料格式（小端、packed，永不就地修改）

```
偏移 0    MaterialHeader (40B): magic="NMTS", version, width, height,
                               instance_count, pixel_offset=40,
                               instance_offset, eps_*
偏移 40   RGBA8 像素区, width*height*4 字节
偏移 296  InstanceRecord[](每条 56B): case_id, repeat_index,
                               tangent[3], bitangent[3], normal_geo[3],
                               pixel_index, green_flip, reserved
```

结果记录 `ResultRecord` 为 132 字节，字段顺序经 SPIR-V 反射核对，与
std430 数组步长完全一致，包含两个原始文件偏移（64 位，拆成 lo/hi）、
32 字节 SHA-256 摘要、掩码、7 个量化指标。

## 可暂停流程

- 按 `NMTS_CHUNK` 分块处理，每块先追加 `evidence/results.bin`，再原子替换
  `state/checkpoint.json`（先写 `.tmp` 再 rename），崩溃不会产生半写状态。
- `run` 在处理量达到 `NMTS_PAUSE_AFTER` 后于块边界暂停，退出码 **10**。
- `resume` 校验材料 SHA-256 与检查点一致、结果记录数与 `next_instance`
  一致后继续；恢复后的结果与不间断运行逐字节相同。

## 资源不足与降级

- Vulkan 初始化任一步失败（GLFW 不可用、无物理设备、无计算队列、设备/
  管线创建失败）都会记录 `DegradeReason` 并退回 CPU 参考实现。
- `NMTS_FORCE_GPU_OOM=1` 注入显存分配失败，验证 Vulkan→CPU 运行期降级。
- `NMTS_FORCE_CHUNK_FAILURE=1` 注入瞬时内存压力，块大小按 1/2 收缩
  （16→8→4）直到成功；全部事件写入 `event_log.json`。

## 证据与“未修改的原始材料”

```
out_accept/
  original/material.nmts        # 生成后 chmod -w，结束时再次核对 SHA-256
  runs/<run>/state/checkpoint.json
  runs/<run>/evidence/results.bin      # 每条记录含原始偏移与摘要
  runs/<run>/evidence/run_report.json  # 毫秒级 UTC 时间点、降级原因、计数
  runs/<run>/evidence/event_log.json   # 分块、暂停、降级事件时间线
```

每条结果的 `instance_offset` 与 `evidence_offset` 都指向**原始材料文件**
中的实例结构与 RGBA 像素，验收脚本断言它们全部落在文件长度之内。
所有时间点为 ISO-8601 毫秒 UTC（如 `2026-11-22T02:15:40.123Z`）。

## 构建与一键验收

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
./scripts/acceptance.sh           # 输出目录 out_accept/
```

无 root 环境下，Vulkan/GLFW 头库通过 `apt-get download` 解压到
`third_party/local/usr`，CMake 自动指向该前缀；着色器由
`glslangValidator` 在构建期编译并以字节数组嵌入可执行文件，运行时不依赖
任何着色器文件。依赖前缀可用 `scripts/bootstrap_deps.sh` 一键重建。

## 子命令

```sh
nmts_verify generate <out.nmts>
nmts_verify run <run_id>          # 受 NMTS_* 环境变量控制
nmts_verify resume <run_id>       # 忽略暂停阈值，跑至完成
nmts_verify compare [--ignore-backend] <a.bin> <b.bin>
nmts_verify selftest
```

`compare --ignore-backend` 忽略后端标记（CPU=1/Vulkan=2），仅比较校验语义；
无该标志时要求逐字节一致（用于暂停/恢复前后对比）。
