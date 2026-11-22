# Geometry Shader Output Inspector

固定技术栈：C++17、Vulkan、GLFW。项目实现一条可暂停、可跨进程恢复的几何着色器输出检查流程，并以三个锚点保存中间状态和关键证据。

## 三段锚点

- `A0 original-input`：在 Vulkan 上传前保存不可变原始材料。`source_offset=0`，覆盖完整字节范围。
- `A1 geometry-output`：几何着色器写入 `std430` SSBO 后，使用 device-local buffer + staging buffer 回读 64 字节记录。每条记录包含 `p0/p1/color/meta`，`meta.x` 是原始材料字节偏移。
- `A2 composite-output`：从磁盘上的 A1 快照重放线段到 512×512（或 compact 256×256）离屏图像，并回读 RGBA。

每个锚点写入：

- `checkpoints/<anchor>/snapshot.bin`
- `checkpoints/<anchor>/checkpoint.json`
- `timeline.jsonl` 中的 `checkpoint_saved` / `run_paused` / `run_resumed` 事件
- `state.json` 中的下一阶段状态

## 构建

标准环境需要 CMake、C++17 编译器、Vulkan loader/headers、GLFW 3.3+ 和 `glslangValidator`：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

如果仓库根目录存在 `.toolchain/root`（本项目当前环境已解包的无 root 依赖），CMake 会自动使用其中的 Vulkan headers、GLFW 和 `glslang`，无需额外参数。

## 一键验收

```bash
GS_HEADLESS=1 ./build/gs_inspect --acceptance evidence/acceptance-$(date -u +%Y%m%dT%H%M%SZ)
```

该命令依次暂停/恢复 A0、A1、A2，然后从 A0 派生的材料独立重放 A1/A2，逐字节比较前后结果，最后写出 `comparison.json` 和 `acceptance.json`。

当前样例证据：

- `evidence/acceptance-vulkan-final/acceptance.json`
- `evidence/acceptance-vulkan-final/comparison.json`
- `evidence/acceptance-vulkan-final/timeline.jsonl`
- `evidence/acceptance-vulkan-final/checkpoints/02_composite_output/preview.ppm`
- `evidence/acceptance-vulkan-final/checkpoints/02_composite_output/preview.png`

样例中 A1/A2 的原始偏移范围均为 `82..1333`，A1 捕获时间为 `2026-11-22T01:00:01.510Z`，重放时间为 `2026-11-22T01:00:01.569Z`，两次 SHA-256 和全部 1536 字节一致。

## 真正跨进程暂停/恢复

```bash
GS_HEADLESS=1 ./build/gs_inspect --run evidence/my-run
# 退出码 20：已暂停在 A0
GS_HEADLESS=1 ./build/gs_inspect --resume evidence/my-run
# 退出码 20：已从磁盘恢复并暂停在 A1
GS_HEADLESS=1 ./build/gs_inspect --resume evidence/my-run
# 退出码 0：从 A1 快照继续 A2、重放比较并验收
```

可用 `scripts/run_resume_chain.sh` 自动执行上述链路。

## GUI 与 GLFW

```bash
./build/gs_inspect --gui evidence/my-gui-run
```

每个锚点保存后窗口标题显示暂停状态，按 `SPACE` 或 `ENTER` 恢复下一阶段，`ESC`/关闭窗口退出。命令行模式使用 GLFW null platform，因此不需要显示服务器。

## 资源不足与降级

资源计划在 `resource_plan*.json` 中固定记录设备名、Vulkan 版本、几何能力、SSBO 上限、堆大小、预算、需求量和决策原因。

- `--degrade auto`：优先 full；超出预算时尝试 compact；仍不足则 CPU。
- `--degrade full`：预算不足时直接进入 CPU 降级。
- `--degrade compact`：强制 256×256 离屏目标，仍走 Vulkan GS。
- `--degrade cpu`：显式使用同算法的 CPU 几何展开与软件栅格化。
- `--budget-mib N`：固定显存预算，用于稳定复现降级路径。
- `--force-low-memory`：按 full 需求的一半作为预算。
- 初始化或运行期任一次 Vulkan 分配/执行失败，都会把原因追加到资源计划并切换到 CPU，不丢失已保存锚点。

已覆盖样例：

- `evidence/acceptance-compact-20261122`：Vulkan compact，256×256。
- `evidence/acceptance-lowmem2`：2 MiB 预算自动选 compact。
- `evidence/acceptance-lowmem1`：1 MiB 预算自动降级 CPU。
- `evidence/acceptance-cpu-final`：显式 CPU 降级。

## 原始材料不可变

原始材料是 `material/geometry_input.gsi`。运行开始时：

1. 计算原文件 SHA-256 和字节数。
2. 原样复制到 `<run>/original/geometry_input.gsi`。
3. 校验副本哈希必须等于原文件。
4. 恢复、比较和验收时重新计算原文件及副本哈希。

`acceptance.json` 同时记录 `sha256_at_capture`、`sha256_now`、`preserved_sha256` 和 `unchanged`。任何修改都会导致验收失败，而不是静默继续。

## 证据字段

`acceptance.json` 的每个检查点都落到具体时间和原始偏移：

```json
{
  "captured_at_utc": "2026-11-22T01:00:01.510Z",
  "captured_epoch_ms": 1795309201510,
  "captured_monotonic_ns": 28133088207456,
  "source_offset": 82,
  "last_source_offset": 1333,
  "source_end_offset": 1446,
  "payload_path": "checkpoints/01_geometry_output/snapshot.bin",
  "payload_offset": 0,
  "payload_sha256": "e1e64f12f7950e6b4b44d609c3c849b4b4f788e88eed15f9740d0bcd6ef7fa0d"
}
```

`comparison.json` 同时保留 before/after UTC 时间、单调时钟、SHA-256、大小、源偏移和首个差异字节偏移。完全相同时 `first_difference_offset` 为 `null`。

## 实现要点

- `shaders/capture.geom`：静态展开三条边，写入 A1 记录并发射线段。
- `shaders/replay.vert`：只依赖 A1 SSBO 重建 A2，证明恢复确实使用中间状态。
- `src/vulkan_engine.cpp`：Vulkan 设备/显存探针、离屏 render pass、两个管线、显式 staging 拷贝。
- `src/checkpoint.cpp`：状态机、原子快照发布、完整性校验、追加式时间线。
- `src/cpu_backend.cpp`：与 GPU 记录布局一致的降级实现。
- `src/app.cpp`：命令编排、跨进程恢复、比较和验收。

本机 llvmpipe 的 `gl_PrimitiveID` 在几何着色器中恒为 0，因此 primitive id 由顶点着色器用 `gl_VertexIndex / 3` 以 flat varying 传入。该方式不依赖驱动对 `gl_PrimitiveID` 的具体实现。

## 测试

```bash
ctest --test-dir build --output-on-failure
```

CTest 执行一次完整 Vulkan/CPU 可用路径验收，验证原始材料不变、三个快照存在且重放结果逐字节一致。
