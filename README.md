# 材质实例热更新：可回放实验

本实验在 C++17 上实现材质实例热更新的确定性状态机，并使用 Vulkan 队列与
GLFW 隐藏窗口执行真实 GPU 缓冲上传、读回和资源晋升。每条命令都记录输入、
变更前状态、结果、变更后状态和证据；事件通过 SHA-256 链式哈希串联，可逐字节
回放。

## 实验场景

| 场景 | 作用 |
|---|---|
| `cancel_primary` | 在 `upload_begin` 后、`upload_complete` 前执行一次中途取消；随后提交必须失败，旧材质保持不变。 |
| `cancel_control` | 使用相同载荷和取消点，再额外取消一次；验证重复取消为 no-op，并与主取消结果一致。 |
| `no_cancel_baseline` | 相同载荷不取消，正常完成提交，排除载荷或正常路径导致的失败。 |
| `reordered_events` | 先发送越序上传和提交，均须拒绝；再按合法顺序完成更新。 |
| `duplicate_submit` | 相同 key/载荷去重；相同 key/不同载荷冲突；只产生一个事务和一次版本递增。 |

取消点是**事务状态机的中途**（`uploading`，暂存 GPU 资源仍存在），不是
“硬件队列此刻仍在执行”的实时证明。报告的人工确认区单独列出了这一点。

## 构建

依赖：CMake 3.20+、C++17 编译器、Vulkan loader/headers、GLFW 3.3+。

```bash
sudo apt install libvulkan-dev libglfw3-dev vulkan-tools
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

没有 root 权限时，可把开发包解到仓库内 `.deps`（CMake 会自动识别）：

```bash
./scripts/bootstrap_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

如果依赖安装在其它 sysroot：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sysroot
```

## 运行

真实 Vulkan/GLFW：

```bash
./build/material_hotupdate --backend vulkan --output artifacts/vulkan-run
```

无 GPU/无显示环境的确定性对照：

```bash
./build/material_hotupdate --backend fake --output artifacts/fake-run
```

独立回放（要求原目录存在 `events.jsonl` 以比较哈希）：

```bash
./build/material_hotupdate --replay artifacts/vulkan-run/commands.jsonl \
  --backend fake --output artifacts/vulkan-replay
```

软件 Vulkan 驱动示例：

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/material_hotupdate --backend vulkan --output artifacts/vulkan-run
```

退出码：`0` 全部自动检查与回放通过；`1` 实验断言或回放失败；`2` 初始化或
运行异常。

## 证据

每次运行在 `--output` 目录生成：

- `commands.jsonl`：可重放输入，每行一条命令。
- `events.jsonl`：每步输入、前后状态、结果、证据、`prev_hash` 与 `event_hash`。
- `replay.json`：原始运行内部回放的步数和最终哈希。
- `checks.json`：自动断言明细。
- `final_state.json`：最后一步后的状态快照。
- `environment.json`：编译器、后端、设备与驱动信息。
- `report.md`：自动结论与人工确认分区报告。
- `manual_confirmation.md`：待人工填写的确认单。
- `SHA256SUMS`：关键产物文件哈希。

事件哈希覆盖 `seq/scenario/scenario_seq/op/input/state_before/result/state_after/evidence`。
`commands.jsonl` 与 `events.jsonl` 足以复核每一步，不需要重新推导场景。

## 自动结论与人工确认

`report.md` 分两个独立部分：

- **自动结论**：只依据场景断言、状态不变量和逐字节回放哈希。
- **人工确认**：取消语义、幂等键作用域、越序拒绝策略、取消时序定义、目标硬件
  与视觉验收；程序不会把这些项目计为通过。

当前环境已验证 `llvmpipe` 软件 Vulkan 路径；它证明实现可执行，不代表目标硬件
验收。

## 失败后的操作

初始化失败时，程序仍尝试写入 `failure.txt`、`environment.json` 和包含可执行
命令的 `report.md`。常用排查顺序：

```bash
sudo apt install libvulkan-dev libglfw3-dev vulkan-tools
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/material_hotupdate --backend fake --output artifacts/fake-retry
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/material_hotupdate --backend vulkan --output artifacts/vulkan-retry
```

先确认状态机与回放在 `fake` 后端通过，再单独修复 Vulkan loader、ICD 或显示环境。
