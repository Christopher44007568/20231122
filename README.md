# 实例化模型选取 · 可回放实验

固定采用 **C++17 + Vulkan + GLFW** 的离屏实验台。它对一批模型提交执行
“选取 → 去重 → 实例化绘制”，同时保留旧版 `legacy-v1` 与新版
`stable-v2` 两个选择快照作为对照，并把每一步的输入、前后状态和结果写入
可回放 JSONL。

默认实验会验证：

- 输入格式按 magic 识别，不依赖扩展名；一个未知扩展/未知 magic 文件被隔离。
- 打乱提交顺序后，`stable-v2` 的选择哈希不变；旧版 `legacy-v1` 能观察到顺序敏感性。
- 完全相同的重复提交被去重，选择哈希不变。
- 异常对象之后仍继续加载并渲染其余对象。
- 两个版本快照分别通过 Vulkan 实例化绘制并读回，差异图与 draw 证据保留。
- 结束前删除 `.tmp/` 临时状态，并再次检查其不存在。
- `trace.jsonl` 可只依赖保留的输入证据完整回放，逐项校验对象、顺序和重复探针。

## 构建与运行

```bash
./run_experiment.sh
```

等价步骤：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
build/generate_fixtures sample_input
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  build/instance_select run --input sample_input --out artifacts/evidence
```

单独回放：

```bash
build/instance_select replay \
  --trace artifacts/evidence/trace.jsonl \
  --out artifacts/evidence/replay/report.json
```

退出码：`0` 表示全部不变量和回放校验通过；`1` 表示实验检查失败；
`2` 表示输入目录不存在；`3` 表示初始化或回放文件异常。

依赖已收敛在 `third_party/`：Vulkan 头文件、GLFW 3.4 null 平台静态库与
预编译 SPIR-V。运行时需要 Vulkan loader 和 ICD；当前环境默认使用 lavapipe。
GLFW 只用于无窗口平台上初始化 Vulkan，不创建 surface 或 swapchain。

如需重新编译着色器：

```bash
GLSLANG_VALIDATOR=/path/to/glslangValidator ./scripts/compile_shaders.sh
```

## 输入格式

两种容器都使用 32 字节文件头，文件格式只由 magic 判定：

| 版本 | magic | 记录大小 | 选择字段 |
|---|---|---:|---|
| Legacy v1 | `IMD1` | 48 B | `legacySelector` 隐式选择 |
| Current v2 | `IMD2` | 64 B | `legacySelector` + 显式 `model` |

公共头字段：

```text
u32 magic
u16 version
u16 headerBytes = 32
u32 batchId
u32 recordCount
u32 flags
u32 reserved
u64 reserved
```

记录字段为对象 ID、逻辑修订号、旧选择码、显式模型、平移、缩放、颜色和
绕 Y 轴旋转。模型目录固定为 `0=cube`、`1=pyramid`、`2=plane`。

`legacySelector` 模拟旧版隐式查表；`model` 是新版显式选择。正是这两个字段
的差异，让同一批提交在两个版本快照中绘制出可对照的画面。

## 两个版本快照

| 快照 | 模型来源 | 冲突策略 | 输出顺序 |
|---|---|---|---|
| `legacy-v1` | `legacySelector % 3` | 首个到达者保留 | 到达顺序 |
| `stable-v2` | `model % 3` | 最高修订号；同修订按内容稳定决胜 | 对象 ID 排序 |

两个快照都按对象 ID 去重。完全相同的载荷记为
`duplicate_suppressed`；新版更高修订记为 `superseded`；旧版冲突记为
`conflict_kept_first`。

默认夹具包含 `alpha`、`beta`、`gamma`、`delta` 等对象，其中 `beta`
同时有 v1/v2 两个修订。规范顺序下，`legacy-v1` 保留先到的 v1，
`stable-v2` 选择 v2，因此两张图像应产生可见差异。该差异是预期的对照结果，
不是渲染错误。

## 顺序改变与重复提交

每次运行都从同一提交集合生成三份输入序列：

- `canonical`：文件按名称排序、记录按文件内顺序。
- `reversed`：整体逆序。
- `shuffled`：固定种子 `0x51A7E5` 的 `std::mt19937` 洗牌。

验收要求 `stable-v2` 在三种顺序下选择哈希完全一致，同时
`legacy-v1` 至少出现一次不同，证明顺序改变不是被静默忽略。

重复提交探针在完整集合后再追加一条与首个提交完全相同的记录。两个快照都必须
保持选择哈希不变，并让 `duplicate_suppressed` 计数增加。

## 未知扩展与异常隔离

默认夹具 `sample_input/mystery_payload.qzv` 具有未知扩展名和未知 magic
`IMDX`。解析抛出 `InputFormatError` 后，该对象只记为
`isolated_error`，不终止批处理。

按文件名排序，紧随其后的 `zz_after_poison.any` 仍会被加载并进入后续选择与
渲染。`manifest.json` 中同时记录 `loaded=4` 与 `skipped=1`，
`exception_isolated=true`。这也验证了扩展名不参与格式分派：合法的
`.imd1`、`.imd2` 和 `.any` 都按 magic 读取。

## Vulkan 实例化与证据

渲染目标为 640×400 的离屏 color + depth。三个模型目录合并到网格缓冲，
选取结果按模型分组，每个非空组执行一次 `vkCmdDrawIndexed`，并通过
instance-rate 顶点属性传入实例矩阵和颜色。默认快照有 3 个 draw：

```text
cube    : instanceCount=2
pyramid : instanceCount=3
plane   : instanceCount=2
```

每个快照记录 draw 参数、实例缓冲偏移、图像哈希、有序 image layout
transition、设备名和完整选取实例。读回 RGBA 后写出 PPM，并生成
`legacy | stable | 8x 差异` 三联图。

## 回放

`trace.jsonl` 每行至少包含：

```text
seq, timestamp, step, input, state_before, result, state_after, ok, error, check
```

`replay_verify` 只使用 trace 中记录的保留输入目录重新执行解析、三种顺序和
两个选择策略，然后核对：

- 每个成功对象的记录哈希和记录数；
- 每个 `policy × order` 的选择哈希与绘制顺序哈希；
- 两种策略的重复提交前后哈希；
- 最终选择哈希、成功/跳过对象数与提交总数。

报告写入 `replay/report.json`；`mismatches=0` 才算通过。

## 证据布局

```text
artifacts/evidence/
  trace.jsonl                 # 每步输入、前后状态、结果、错误与回放摘要
  manifest.json               # 总体结论、计数、最终哈希、证据索引
  summary.txt                 # 人工可读结论
  snapshots.json              # 两版快照的实例、draw、transition、图像哈希
  correlation.json            # 两版画面差异与预期对照结论
  order_matrix.json           # canonical/reversed/shuffled × 两策略哈希
  selections/
    legacy_v1.json            # 旧版完整选择与冲突事件
    stable_v2.json            # 新版完整选择与冲突事件
  images/
    legacy_v1.ppm             # 旧版快照
    current_v2.ppm            # 新版快照
    diff.ppm                  # 旧版 | 新版 | 8x 放大差异
  inputs/                     # 输入原样保留，包括未知扩展异常样本
  replay/report.json          # 回放重算结果与 0/非 0 mismatch
```

`.tmp/` 只在运行中存在，用于暂存待渲染的选择摘要；`cleanup` 步骤删除它并
记录 `exists_after=false`。证据目录中的其他文件不是临时状态，会被保留。

默认夹具的三次顺序结果应满足：

```text
stable-v2 : canonical == reversed == shuffled
legacy-v1 : canonical != reversed（默认样本）
duplicate : before_hash == after_hash
```

## 源码地图

- `src/format.*`：`IMD1`/`IMD2` 读取、magic 分派、字段校验与夹具写出。
- `src/selection.*`：双策略选择、最高修订、稳定决胜、重复抑制和状态哈希。
- `src/journal.*`：JSONL 写入与回放所需的字段提取。
- `src/evidence.*`：RGBA 差异、PPM 三联图和图像哈希。
- `src/vk_context.*`：GLFW null 平台、Vulkan instance/device/命令池。
- `src/renderer.*`：网格目录、instance-rate 属性、离屏 pass 与读回。
- `src/experiment.*`：批处理、异常隔离、顺序矩阵、证据、清理和回放校验。
- `src/main.cpp`：`run` / `replay` CLI。
- `tools/generate_fixtures.cpp`：确定性输入与未知扩展异常样本。
- `tests/core_tests.cpp`：格式、顺序敏感/稳定和重复去重回归测试。
