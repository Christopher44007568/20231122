# 屏幕拾取深度核对

一个针对屏幕拾取/深度比较中“缺失片段”的局部修正工具。技术栈固定为
C++17、Vulkan、GLFW，不依赖 ImGui 等 UI 框架。

工具把同一场景拆成可独立重跑的对象：先记录正常基线，再用一组极限深度参数
重跑指定对象，定位缺失片段边界，自动搜索局部深度修正，并把前后图像、对象 ID、
深度样本、候选参数和对比结果写入可恢复的会话目录。

## 能做什么

- 9 个可拾取渲染对象，点击场景或 `Up`/`Down` 切换焦点。
- 4 组极限参数：精度漂移、深度偏置风暴、深度钳制、极窄深度范围。
- 每个对象独立重跑，不要求整场重跑。
- 自动局部修正：搜索一组对象级深度偏置，按缺失/新增片段数量选择候选。
- 4 种查看模式：基线、局部修正、左右分屏、片段差异。
- 每个对象单独导出图像和证据包。
- 退出、恢复、再继续；每完成一个对象就原子更新检查点。
- 缺失片段左右边界各取边界外/边界内四个采样点，记录对象 ID、深度和颜色变化。

默认场景中的 `epsilon-overlap` 与相邻面板深度差很小。`bias-storm` 预设会把它
推到相邻面板之后，产生稳定的大块缺失区域；自动修正会搜索回零偏置并恢复。

## 构建

Ubuntu/Debian 系统依赖：

```bash
sudo apt install cmake g++ libvulkan-dev libglfw3-dev glslang-tools libstb-dev
```

没有 root 时，可把开发包解压到项目内：

```bash
./scripts/bootstrap_deps.sh
```

构建与测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 运行

窗口模式：

```bash
./build/bin/depth_recheck --session sessions/case-a
```

无显示环境或批处理：

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/bin/depth_recheck --headless --fresh --all --session sessions/smoke
```

常用参数：

- `--session <dir>`：检查点和证据目录。
- `--fresh`：清空该目录中的旧检查点和证据，重新开始。
- `--headless`：不创建 GLFW 窗口，直接 Vulkan 离屏渲染。
- `--all`：启动后逐对象重跑并导出。
- `--frames <n>`：窗口模式仅提交 n 帧后保存退出，用于快速验证。

## 操作

| 输入 | 行为 |
| --- | --- |
| 鼠标左键 | 从基线对象 ID 缓冲拾取对象 |
| `Up` / `Down` / `Tab` | 切换对象并立即重跑 |
| `R` / `F5` / `Enter` | 重跑当前对象并自动搜索局部修正 |
| `A` | 逐对象重跑、比较、导出全部对象 |
| `P` | 切换极限参数预设并重跑当前对象 |
| `1` `2` `3` `4` | 基线 / 修正结果 / 左右分屏 / 差异 |
| `V` / `Space` | 循环查看模式 |
| `E` | 重新导出当前对象证据包 |
| `F2` | 手动保存检查点 |
| `Esc` | 保存并退出 |

`A` 执行期间关闭窗口，会在当前对象完成后停止并保留已完成结果。下次启动同一
`--session` 目录会载入对象列表、参数、选择位置、指标和边界样本。

## 证据目录

```text
sessions/case-a/
├── state.v1                  # 原子检查点
└── evidence/
    ├── baseline.png          # 正常基线
    ├── runs.log              # 追加式重跑历史
    └── 05-epsilon-overlap/
        ├── extreme.png       # 极限参数结果
        ├── corrected.png     # 局部修正结果
        ├── diff.png          # 极限结果相对基线的缺失/新增片段
        ├── corrected_diff.png
        ├── compare.png       # 左基线、右修正结果
        ├── report.json       # 指标、参数、边界样本、证据索引
        ├── boundary.csv      # 边界前后样本
        └── candidates.csv    # 自动修正候选及得分
```

`boundary.csv` 为每条边提供两个位置：

- `*-outside`：缺失区域边界外一个像素。
- `*-inside`：缺失区域边界内一个像素。

每行同时包含基线 `before_*` 和重跑 `after_*` 的对象 ID、深度，以及
`depth_delta`、`object_changed`、`color_changed`。

`state.v1` 使用稳定的行式 `key|value` 格式，写入时先落
`state.v1.tmp`，随后 `rename` 覆盖正式文件，避免中途退出留下半个检查点。

## 实现要点

- 场景 pass 使用三个附件：`RGBA8` 颜色、`RG32F` 拾取数据
  `(gl_FragDepth, objectId)`、`D32_SFLOAT` 深度。
- 极限深度偏置在片元着色器中以归一化深度单位施加；这样 `objectId` 和边界
  深度来自同一最终深度值，跨驱动结果更可复现。
- 比较在 CPU 端逐像素进行：基线中属于焦点对象、重跑中不再属于焦点对象的
  像素计为缺失；反向变化计为新增。
- 缺失区域包围盒决定边界采样位置；没有缺失时退化为对象投影包围盒。
- 自动修正从极限偏置、零偏置和若干比例偏置中搜索，优先减少缺失片段，再减少
  新增片段和整像素对象变化。
- 基线深度图、极限图、修正图和差异图都从同一 Vulkan 离屏目标读回，导出前
  等待 GPU 队列空闲。

## 代码结构

- `src/scene.*`：默认场景与对象投影。
- `src/params.*`：极限预设和修正候选。
- `src/compare.*`：逐像素比较、边界采样、差异图、分屏图。
- `src/session.*`：会话、证据路径、原子断点。
- `src/report.*`：JSON/CSV 证据包。
- `src/renderer_*`：Vulkan 设备、离屏 pass、管线、读写和呈现。
- `src/app*`：交互、逐对象状态机、恢复和导出。

要接入真实渲染器，替换 `Scene::makeDefault()` 的对象数据和
`Renderer::renderTarget()` 的场景提交即可；比较、断点、导出和 UI 层不需要改。
