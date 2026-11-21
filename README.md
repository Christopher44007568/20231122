# 曲面裁剪平面局部修正工具

一个固定采用 **C++17 + Vulkan + GLFW** 的桌面工具，用三个锚点构造局部弯曲裁剪面，对每个渲染对象独立执行修正、重跑、比较和导出。

| 修正视图 | 差异视图 |
|---|---|
| ![修正视图](artifacts/evidence/gui_corrected.png) | ![差异视图](artifacts/evidence/gui_diff.png) |

## 核心能力

- 三个锚点 A/B/C 各带位置、法线和**不可变原始偏移**；当前应用偏移可以调整，但原始偏移始终从只读材料读取。
- 二次 Bézier 裁剪曲线沿法向扫掠成局部曲面；在带宽 `0.42 mm`、横向宽度 `1.70 mm` 内使用平滑权重把顶点拉回裁剪面。
- 三个渲染对象各自维护运行历史。可以只重跑一个对象，也可以一次重跑全部对象。
- 每个对象支持“原始 -> 最新运行”和“上一次运行 -> 最新运行”两种比较。
- 每个对象可以单独导出 OBJ 和对应 JSON 元数据。
- GUI 按 `Esc` 或关闭窗口会写入 `session_suspended` 证据；下一次启动自动恢复并写入 `session_resumed`。
- 证据链采用逐事件 SHA-256 前向哈希，可检测缺行和内容篡改。
- 原始材料在 `materials/raw` 保留一份，在工作区 `workspace/original_material` 再保留只读副本；每次运行、验收都会校验文件级和聚合哈希。

## 构建

依赖已随仓库放在 `third_party/`：Vulkan 头文件与 loader、GLFW 3.4、Dear ImGui、STB、nlohmann/json。构建不需要 sudo，也不依赖系统开发包。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

产物：`build/curved_trim`。运行时 loader/GLFW 动态库会复制到同一目录，并以 `$ORIGIN` 查找。

## 快速开始

```bash
# 首次启动会自动创建 workspace，并从 materials/raw 复制只读原始材料
./build/curved_trim

# 无界面逐对象操作
./build/curved_trim init
./build/curved_trim run --object seg02_saddle --offsets=-0.40,0.20,-0.10
./build/curved_trim compare --object seg02_saddle
./build/curved_trim export --object seg02_saddle

# 中途退出后，任意下一条命令都会恢复会话
./build/curved_trim checkpoint --reason "lunch_break"
./build/curved_trim status

# 生成验收报告与时间线
./build/curved_trim accept --out artifacts/acceptance
```

`--offsets` 只修改当前应用偏移。签入/只读材料中的原始偏移仍为：

```text
A = -0.350000 mm
B =  0.180000 mm
C = -0.120000 mm
```

## GUI 操作

- 左侧锚点滑块：调整当前应用偏移；`Raw` 按钮恢复该锚点的原始偏移。
- 每个对象卡片：`Re-run`、`Compare`、`Export`，三者都只作用于该对象。
- `Run all` / `Export all`：批量执行，但每次运行仍分别写入对象自己的 run 目录。
- `Corrected` / `Original` / `Diff`：切换最新修正、原始材料和逐顶点位移着色。
- `Clip surface`：显示三锚点扫掠曲面；`Wireframe`：选中对象线框叠加。
- 鼠标左键拖动视口旋转，滚轮缩放。
- `Save & Exit`、`Esc`、窗口关闭都会保存会话并记录退出时间。

无窗口冒烟测试和截图：

```bash
./build/curved_trim gui --frames 8 \
  --screenshot artifacts/evidence/gui_corrected.png --shot-frame 3
```

## 数学与对象流水线

三个锚点位置 `P0/P1/P2` 和法线 `N0/N1/N2`，加上当前偏移 `d0/d1/d2`，构造控制点：

```text
Qi = Pi + normalize(Ni) * di
C(t) = (1-t)^2 Q0 + 2(1-t)t Q1 + t^2 Q2
```

对曲线切线 `T(t)` 取扫掠副法线 `B(t)`，裁剪面为 `C(t) + s B(t)`。对每个顶点求最近曲线参数 `t`，得到横向量 `s` 和法向有符号偏移 `d`：

```text
bandWeight  = smoothstep(1 - |d| / 0.42)
widthWeight = smoothstep(1 - |s| / 1.70)
delta       = -d * bandWeight * widthWeight
```

每个对象都从只读 OBJ 独立计算、独立写 run，不共享可变的修正网格。这保证单对象重跑和比较不会受其他对象状态影响。

## 持久化与证据

```text
materials/raw/                          # 只读原始材料（随项目保留）
workspace/
  original_material/                    # 字节级只读副本，权限 0444
  session.json                          # 原子写入的会话状态
  runs/<object>/<run>.obj               # 修正后网格
  runs/<object>/<run>.offsets.csv       # 全量顶点偏移证据
  runs/<object>/<run>.json              # 运行元数据与关键顶点
  comparisons/<object>_compare_*.json   # 比较报告
  exports/<object>_<run>.obj            # 单对象导出
  evidence/evidence.jsonl               # 追加式哈希链
  evidence/chain_head.json              # 最新事件序号与哈希
artifacts/acceptance/
  acceptance_report.json
  acceptance_report.md
  evidence_timeline.csv
```

每次运行记录开始/结束 UTC 毫秒时间、本地时区时间、epoch 毫秒、原始偏移、当前应用偏移、对象 ID、run ID、受影响顶点数、修正前后统计、OBJ/CSV 哈希和关键顶点。

完整顶点偏移 CSV 每行都带 `run_finished_utc` 和该 run 的原始/应用偏移，例如：

```text
run_finished_utc,run_id,object_id,vertex_index,curve_t,lateral_mm,before_offset_mm,...
2026-11-21T22:26:51.715Z,run_seg01_ridge_..._4,seg01_ridge,564,0.401...,0.41...,0.184182...,...
```

`session_suspended` 和 `session_resumed` 事件使中途退出/重新进入成为可审计证据，而不是仅依赖内存状态。

## 验收口径

`accept` 会在每个对象上分别执行“重跑 -> 与原材比较 -> 导出”，并执行一次真实的
`suspend -> Workspace::open -> resume` 探针。报告必须通过以下检查：

- 原始材料源目录与工作区只读副本均校验通过，且聚合 SHA-256 一致。
- 三个锚点存在，原始偏移文本精确回读为 `-0.350000 / 0.180000 / -0.120000 mm`。
- 三个对象各有至少两次独立运行，run ID 唯一，开始/结束时间具体且结束不早于开始。
- 每个 run 的原始偏移等于材料值；OBJ 和全量偏移 CSV 存在且哈希一致。
- 每个对象都有比较报告和导出 OBJ。
- 证据链连续且每个事件内容哈希匹配。
- 证据中同时存在 `session_suspended` 与 `session_resumed`。

报告同时输出 Markdown 表，逐对象列出具体运行时间、原始偏移、应用偏移、受影响点数、最大修正、输出哈希和抽样顶点修正前后偏移。

## 命令行

```text
curved_trim [gui] [--workspace DIR] [--frames N]
            [--screenshot FILE --shot-frame N]
            [--view corrected|original|diff] [--no-surface] [--wireframe]
curved_trim init [--workspace DIR] [--material DIR]
curved_trim run --object ID|all [--offsets A,B,C]
curved_trim run-all [--offsets A,B,C]
curved_trim compare --object ID [--previous]
curved_trim export --object ID|all [--out DIR]
curved_trim status [--json]
curved_trim verify
curved_trim checkpoint --reason TEXT
curved_trim accept [--out DIR] [--no-fill]
```

环境变量：`CLIPFIX_WORKSPACE`、`CLIPFIX_RAW_DIR`、`CLIPFIX_ARTIFACTS`。
`CLIPFIX_FORCE_X11=1` 可在 GLFW 同时支持 Wayland/X11 时强制 X11。

## 代码结构

- `src/core/`：裁剪数学、OBJ I/O、SHA-256、原始材料校验、会话、run/compare/export、验收。
- `src/app/vulkan_app.cpp`：Vulkan instance/device/swapchain/render pass/pipeline、截图、动作执行。
- `src/app/scene_builder.cpp`：原始/修正/差异几何与三锚点曲面可视化。
- `src/app/ui.cpp`：对象列表、锚点滑块、证据与显示控制。
- `tests/acceptance_flow.sh`：进程级“运行 -> 退出 -> 重进 -> 比较 -> 导出 -> 验收”回归。

## 已知边界

- UI 使用默认 ImGui 字体，因此界面文案为英文；Markdown/JSON 验收报告为中文/结构化字段。
- 截面偏移单位固定为毫米；样例材料为 `materials/raw` 中的三块曲面演示网格。
- GUI 需要可用的 Vulkan ICD 和窗口系统；没有显示服务时使用上述 CLI 命令完成全部验收。
- 随仓库携带的依赖仅用于可复现构建，许可证文本位于 `third_party/licenses/`。
