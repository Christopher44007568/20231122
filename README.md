# 离屏截图一致性可回放实验

固定技术栈：**C++17 + Vulkan + GLFW**。程序用 Vulkan 渲染到离屏图像，经
`vkCmdCopyImageToBuffer` 回读 RGBA 像素，再写为 PNG。GLFW 只负责无显示环境下的
平台生命周期，截图不依赖窗口或 swapchain。

## 实验内容

1. 读取 `data/canonical.record` 的 12 条确定性事件，生成基准轨迹。
2. 在事件 4 后保存检查点，并生成一条真实的未换行截断记录。截断尾部不会被误解析。
3. 独立重放截断记录中的 5 条完整事件，并与基准逐步比对。
4. 从该前缀末态生成检查点；恢复后校验视觉、协议和帧哈希，再重放事件 5-11。
5. 交换事件 8 和 9 生成乱序分支；定位每个状态，报告首个状态/帧分歧。
6. 重复投递同一事件、以及用新事件 id 重复提交同一 token，验证幂等性。
7. 每个状态在默认配置下渲染两次；两次像素 SHA-256 必须相同。

每步记录以下证据：

- 输入：原始事件行或检查点文件。
- 状态：变更前视觉哈希、完整视觉状态、协议状态与帧输入摘要。
- 结果：应用/丢弃/去重状态、帧 SHA-256、PNG 路径和二次截图是否一致。

## 构建

Ubuntu/Debian 依赖：

```bash
sudo apt install build-essential cmake libvulkan-dev libglfw3-dev mesa-vulkan-drivers
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

没有硬件 GPU 时可用 Mesa lavapipe：

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/offscreen_replay --out artifacts/offscreen-consistency
```

可选参数：

```text
--record <path>      基准记录，默认 data/canonical.record
--out <dir>          证据目录，默认 artifacts/offscreen-consistency
--width/--height     离屏图像尺寸，默认 320x200
--no-double-capture  关闭精确二次渲染校验
```

输出目录会被本次运行重建，适合保存为一次完整实验快照。

## 记录格式

制表符分隔，完整事件必须以换行结束：

```text
event_id<TAB>operation<TAB>arg0<TAB>arg1
```

支持的操作：

- `PATCH bg|accent <rrggbb>`
- `ADD badge|stripe <count>`
- `RENAME title <text>`
- `SUBMIT <token>`：token 只接受一次；新 event id 的重复 token 结果为
  `duplicate_submit_deduped`。

同一 `event_id` 再次出现时结果为 `duplicate_event_dropped`，不改变状态。

## 证据与首个错误

一次成功运行会生成：

- `report.md` / `report.json`：汇总、设备信息、首个错误状态。
- `first_error.json`：单独记录首个错误状态，不使用总分代替定位。
- `first_error_side_by_side.png` / `first_error_diff.png`：首个错误状态并排图与差异热图。
- `comparisons.jsonl`：每一步的视觉状态、帧、顺序和协议状态比较。
- `inputs/truncated.record`：未换行截断输入及其后的恢复尾部记录。
- `runs/*/trace.jsonl`：每一步输入、状态和结果。
- `runs/*/frames/*.png`：对应 Vulkan 离屏截图。
- `evidence.sha256`：所有证据文件的内容哈希。

当前固定场景中，基准与恢复分支完全一致；乱序分支在逻辑步骤 8 首次出现错误，
因为事件 9 被提前到事件 8 之前。报告同时给出该状态的实际/期望视觉 SHA-256、
实际/期望帧 SHA-256，以及两张 PNG 的路径。

修改 `shaders/` 后可用 `tools/compile_shaders.sh` 重新生成内嵌 SPIR-V 数组；
脚本需要 `glslangValidator`。
