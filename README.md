# 分块地形加载 · 可回放实验

基于 **C++17 + Vulkan + GLFW** 的分块地形加载实验台：每一步加载记录
输入 / 状态 / 结果到 JSONL 日志，日志可完整回放并逐条校验。

## 构建与运行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/terrain_exp all --out artifacts/evidence   # 完整实验矩阵
./build/terrain_exp replay --journal artifacts/evidence/run_v1_row/journal.jsonl --out /tmp/r.json
```

退出码：`0` = 全部校验通过，`2` = 校验失败。

## 实验设计

- **两个版本快照对照**：`v1`（两个八度）与 `v2`（三个八度）地形生成算法，
  分别跑出 `run_v1_row` / `run_v2_row`，最终状态哈希必须不同（对照组区分度）。
- **顺序改变**：`run_v1_shuffled` 以固定种子打乱的顺序加载同一批块，
  最终状态哈希必须与 `run_v1_row` 一致（顺序无关性）。
- **重复提交**：对已加载块再次提交被去重（`duplicate_suppressed`，状态哈希不变）；
  相同字节重复上传 Vulkan buffer 后读回哈希一致（`repeated_upload_verify`）。
- **未知扩展**：第 7 个块先通过请求 `VK_EXT_terrain_unknown_probe` 的探针设备加载，
  预期 `VK_ERROR_EXTENSION_NOT_PRESENT`；异常被记录后**继续处理其余块**。
- **临时状态清理**：每轮运行产生的 `tmp/*.raw` 临时高度图在结束时删除并验证。
- **回放**：`replayJournal` 依据日志中的输入重新生成 + 上传 + 读回，
  逐块比对数据哈希并比对最终聚合状态哈希。

## 环境说明

- Vulkan 通过运行时 `dlopen("libvulkan.so.1")` + `vkGetInstanceProcAddr` 加载
  （`src/vkmini.h` 为最小绑定子集），无需安装 SDK；当前环境使用 llvmpipe 软渲染。
- GLFW 默认使用仓库内 headless shim（`shim/GLFW/glfw3.h` + `src/glfw_shim.cpp`），
  API 兼容 GLFW 3.4；窗口创建在无显示环境下按预期失败并被记录为证据。
  有系统 GLFW 时用 `cmake -DTERRAIN_USE_SYSTEM_GLFW=ON` 切换为真实库。

## 证据产物（artifacts/evidence/）

- `bootstrap.jsonl` — GLFW/Vulkan 引导步骤（扩展枚举、设备信息、窗口尝试）。
- `run_*/journal.jsonl` — 每步的输入、前后状态、结果（含异常与清理步骤）。
- `replay_run_*.json` — 回放校验报告。
- `summary.txt` — 三组运行哈希、顺序无关性 / 快照对照 / 回放 / 清理结论。
