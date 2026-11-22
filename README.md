# tcpv — 纹理压缩质量预览（可复核流程）

技术栈：**C++17 / Vulkan / GLFW**。把"纹理压缩质量预览"固化为一条可复核
（auditable）流水线：每次运行产出完整证据链，报告把**自动结论**与**人工确认**
分开呈现，失败或取消时给出可执行的后续操作。

## 构建

```sh
cmake -S . -B build && cmake --build build -j
```

依赖已 vendored 在 `third_party/`（Vulkan-Headers、GLFW null 平台预编译库、
glslangValidator），无需网络与系统 Vulkan SDK；运行时需要 Vulkan ICD
（本仓库已在 llvmpipe/lavapipe 软件渲染上验证）。

## 快速开始

```sh
./build/tcpv doctor                                  # 环境自检
./build/tcpv run --texture photo                     # 完整运行（BC1/BC3/BC7 全覆盖）
./build/tcpv run --texture photo --cancel-after 2    # 中途取消演示
./build/tcpv verify --run <run-id>                   # 复核证据哈希
./build/tcpv confirm --run <run-id> --item 2 --approve --reviewer zhang.wei
./build/tcpv report --run <run-id>                   # 重新生成报告
```

## 证据链（draw call ↔ 资源状态 ↔ 帧缓冲 ↔ 画面差异）

每次运行写入 `runs/<run-id>/`：

- `events.jsonl` — 逐条事件日志。`draw_call`、`resource_state`（图像布局
  迁移）、`framebuffer`、`capture`、`metric`、`cancellation` 事件通过
  `drawCall` 字段互相对应；
- `ref.png` / `dec_<codec>.png` / `diff_<codec>.png` — 参考帧缓冲、压缩解码
  帧缓冲、差异热图（均为 Vulkan 离屏渲染回读）；
- `encoded_<codec>.bin` — CPU 编码的压缩块数据；
- `manifest.json` — 运行清单（schema v2），含全部产物 SHA-256；
- `review.json` / `report.md` — 人工确认清单与最终报告。

## 新旧格式兼容

- 编解码器：BC1、BC3（旧格式）与 BC7（新格式，mode 6）均为内置 CPU
  编解码实现，同一条流水线统一渲染、统一度量；
- 清单格式：`readManifest` 同时接受 schema v1（旧版扁平字段，`tests/`
  下有样例）与 v2（当前版），`verify` 对 v1 做结构校验、对 v2 做哈希复核。

## 中途取消

`--cancel-after N` 在第 N 次 draw call 后触发取消（窗口模式下 ESC/关闭窗口
同样触发）。取消后：已完成步骤的证据照常落盘并哈希，状态记为 `CANCELLED`，
`skippedSteps` 列出被跳过步骤，报告"后续操作"一节给出重跑与核对命令。

## 报告结构

`report.md` 固定四节：

1. **自动结论** — 机器判定（PSNR/误差表、新旧格式覆盖、证据对应关系）；
2. **人工确认** — 待复核清单，`tcpv confirm` 逐条 approve/reject；
3. **后续操作** — 失败/取消/质量不达标时的可执行命令；
4. **证据清单** — 产物路径与 SHA-256，`tcpv verify` 可复核。

## 退出码

`0` 完成且通过 · `3` 运行失败 · `4` 被取消 · `5` 复核发现证据不一致 ·
`2` 参数/用法错误
