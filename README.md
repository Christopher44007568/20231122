# 遮挡查询统计 · 来源检查页

固定技术栈：**C++17 + Vulkan（1.2 核心 API）+ GLFW 3.4**。
无 GPU 环境下使用 Mesa `lvp`（llvmpipe）纯软件 Vulkan 驱动运行，
GLFW 以 `GLFW_NO_API` + 隐藏窗口方式承载 Vulkan，不依赖 swapchain 与 shader 编译器。

## 要解决的问题

对遮挡查询（`VK_QUERY_TYPE_OCCLUSION`）做一次可追溯的来源检查，覆盖：

1. 一次完整执行的基准查询；
2. 一次**录制阶段**中途取消（命令缓冲被重置、从未提交）；
3. 一次**执行阶段**中途取消（帧在途时收到取消信号、读取窗口提前关闭）；
4. 一个**无法判断的边界**（查询槽从未被任何命令触达）。

每个结论都必须能回溯到原始输入与每一步处理，自动结论与人工确认分开呈现，
失败或不确定时给出可执行的后续操作。

## 四个场景与真实 Vulkan 证据

| 场景 | 输入特征 | 关键 API 证据 | 自动结论 |
| --- | --- | --- | --- |
| S1 完整执行 | begin/end 成对、已提交、等待 fence | `VK_SUCCESS` + availability=1 | 被遮挡（样本数 0） |
| S2 录制期取消 | begin 已录制后 `vkResetCommandBuffer`，未提交 | `VK_NOT_READY` + availability=0 | 中途取消 |
| S3 执行期取消 | 工作线程提交后轮询 fence，主线程提前置取消标志 | fence `VK_TIMEOUT` + 取消退出码 2 | 中途取消 |
| S4 无法判断边界 | 控制流跳过，无 begin/end、无提交 | `VK_NOT_READY` + availability=0 + 哨兵样本值 | 无法判断 |

S3 采用生产者/取消者双线程：软件驱动的 `vkQueueSubmit` 是同步执行的，
因此用“工作线程零超时轮询 fence + 主线程置取消标志”稳定复现
“帧完成前取消”的真实工程时序，并保留取消点与排空后两份对照证据。

## 可追溯性设计

- `raw_input_ledger.txt`：四个场景的原始输入账本（查询槽、对象、是否录制/提交）。
- `audit_report.json`：全量机器可读账本，含处理步骤、证据编号（`EV-*`）、
  原始 `vkGetQueryPoolResults` 返回、自动结论、人工确认、后续操作。
- `source_check.html`：来源检查页，按“原始输入 → 处理步骤 → 关键证据 →
  自动结论 → 人工确认 → 后续操作”六段串联，自动/人工两个区块视觉隔离。
- SHA-256：原始输入账本哈希写入 JSON 与 HTML；JSON 账本自身再计算一次哈希。
  SHA-256 为项目内置纯 C++17 实现，无第三方依赖。

## 构建与运行

```bash
# 依赖：g++（C++17）、cmake、libvulkan、glfw3、Mesa vulkan 驱动
./run.sh
# 或手动：
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/occlusion_audit --out out
```

输出位于 `out/`，用浏览器打开 `out/source_check.html` 查看来源检查页。

## 失败时的可执行后续操作

- 初始化失败：检查 `VK_ICD_FILENAMES` 与显示 socket（Wayland/X11），
  无显示时可配合 `Xvfb`；错误信息会在 stderr 给出具体命令。
- 场景级失败/不确定：每个场景的 JSON `follow_up_actions` 与 HTML
  “可执行后续操作”区给出具体动作，例如重发查询、绑定 fence 后再消费、
  返回“无结论”而非用 0 填充样本数。

## 目录结构

```
src/audit_types.h      数据结构与哈希接口
src/evidence.cpp       内置 SHA-256、枚举映射
src/vulkan_runner.*    Vulkan/GLFW 初始化与四个场景
src/report.*           JSON 账本与 HTML 来源检查页
src/main.cpp           串联、哈希落盘、控制台摘要
```
