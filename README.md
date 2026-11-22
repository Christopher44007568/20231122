# 模型包围盒更新：可回放实验

这是一个基于事件回放的模型包围盒更新实验，固定使用 **C++17 + Vulkan + GLFW**。
实验从版本快照 V1 开始，按不同顺序重放同一批更新，并与期望快照 V2 比对。

## 实验设计

- `data/snapshot_v1.json`：回放起点，4 个模型均为版本 1。
- `data/snapshot_v2.json`：期望终态；含 A 的版本 3、B/C 的版本 2，以及异常后保持
  版本 1 的 D。
- `data/events.ndjson`：正常更新、重复事件 ID、非法包围盒，以及第 7 行一条被截断
  的 JSON 对照记录。
- 每个事件包含 `event_id`、`object_id`、`version` 和 `bounds`。
- 同一 `event_id` 只消费一次；旧版本返回 `stale`，不覆盖新状态。
- 非法包围盒按对象隔离：捕获异常、保留旧状态，然后继续处理后续事件。
- 截断 JSON 在进入状态机前被拒绝，单独作为零副作用对照。

场景中的关键验证点：

1. `canonical`：A-v2、B-v2、C-v2、D 非法更新、A-v3、B-v2 重复提交。
2. `reordered`：先提交 A-v3，再提交 A-v2（stale），并在 D 异常后继续提交重复 B-v2。
3. `duplicate_submission`：继承 canonical 的去重集合，再次提交 B-v2，结果必须是
   `duplicate` 且终态哈希不变。
4. `truncated_control`：解析截断记录，结果必须是 `rejected`，终态必须等于 V1。

## 构建

依赖：

- CMake 3.20+
- C++17 编译器
- Vulkan 开发头文件与 loader（`libvulkan-dev`）
- GLFW 3.3+ 开发包（`libglfw3-dev`）
- `glslc` 可选；仓库已包含预编译的 `shaders/*.spv`，缺少 `glslc` 时会直接使用。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

依赖安装在自定义前缀时，增加 `-DCMAKE_PREFIX_PATH=/path/to/prefix`。

## 运行

无窗口回放与验证：

```bash
./build/bbox_replay --render none --output-dir artifacts/latest
```

实际启动 GLFW 窗口并渲染两帧（无显示器时可省略 `--visible`）：

```bash
./build/bbox_replay --render on --frames 2 --output-dir artifacts/latest
```

运行 CTest：

```bash
ctest --test-dir build --output-on-failure
```

## 证据

每次运行会重建输出目录并保留以下内容：

- `run_summary.json`：输入哈希、运行统计、终态哈希、检查项与总结果。
- `canonical_steps.ndjson`、`reordered_steps.ndjson`：逐步记录输入、变更前状态、
  结果、变更后状态。
- `duplicate_submission_steps.ndjson`、`truncated_control_steps.ndjson`：重复提交与
  截断对照证据。
- `*_final_state.json`：各运行终态。
- `snapshot_v1.json`、`snapshot_v2.json`、`events.ndjson`：本次回放使用的输入副本。
- `cleanup.json`：临时目录路径、删除结果和结束后是否存在。
- `vulkan_final_frame.bmp`：`--render on` 时通过 Vulkan swapchain 回读的终帧。
- `evidence_manifest.json`：上述证据文件的 SHA-256 清单。

渲染颜色：绿色为已更新模型，蓝色为未更新模型，红色为发生异常、但被隔离并保留
V1 状态的模型。

## 通过标准

`run_summary.json` 的 `passed` 必须为 `true`，且包含以下检查：

- 两个版本快照成功加载。
- canonical 与 reordered 均匹配 V2，且两者终态 SHA-256 相同。
- 重复事件 ID 被拒绝，终态不变。
- 截断记录被拒绝，终态仍等于 V1。
- D 的非法更新产生一次异常，异常后的对象仍被继续处理，D 保持 V1。
- 临时目录在结束时不存在。
- 指定 `--render on` 时，Vulkan/GLFW 渲染成功。
