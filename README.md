# Shadow Cache Analyzer（阴影缓存分析工具）

针对"光源不动时阴影缓存仍每帧全量重渲染"的问题，本工具为每个
**光源 × 级联（cascade）** 维护独立的阴影深度缓存，按
**光源 / 级联 / 材质** 三个维度记录失效原因，支持单独刷新任意一级缓存，
并对刷新前后的深度图做逐纹素对比，输出刷新次数、耗时与未变化区域比例。

技术栈：C++17 / Vulkan / GLFW。着色器 SPIR-V 由 `src/spirv_gen.cpp`
在运行时直接生成，无需外部 GLSL 编译器。

## 构建与运行

```sh
cmake -B build && cmake --build build -j
# 批量验证（默认）：3 组场景 × 2 种刷新策略，报告写入 artifacts/
xvfb-run -a ./build/shadow_cache_analyzer --validate --frames 60
# 交互查看器（GLFW 窗口显示选中缓存的深度图）
./build/shadow_cache_analyzer --interactive
# 离屏自检：blit 可视化管线
xvfb-run -a ./build/shadow_cache_analyzer --selftest-blit
```

常用参数：`--scenario static|moving-light|material-change|all`、
`--policy baseline|cached|both`、`--frames N`、`--output DIR`。

交互查看器按键：`TAB` 切换光源、`C` 切换级联、`R` 单独刷新当前缓存并
对比前后深度图（结果显示在标题栏并打印到 stdout）、`1/2/3` 切换场景、
`SPACE` 播放/暂停、`ESC` 退出。

## 失效模型

每个缓存项在渲染时快照场景版本号，逐帧 diff 产生失效原因位掩码
（`src/shadow_cache.cpp` 的 `computeReasons`）：

- `first-render`：缓存从未渲染
- `light-transform`：光源位置/方向/张角变化
- `cascade-config`：级联划分/分辨率变化
- `object-transform`：投射物进入/离开级联范围或自身移动
- `material-change`：级联内投射物材质版本号变化（如 alpha-cutout 修改）
- `manual`：用户手动刷新

刷新策略：`baseline` 每帧重渲染所有级别（复现原问题）；`cached` 只重渲染
有失效原因的级别。材质失效按 `<光源>/<级联>/<材质>` 统计。

## 验证结果（llvmpipe，60 帧，256×256/级，5 级缓存）

| 场景 | 策略 | 刷新次数 | 总耗时(CPU) | 未变化区域比例 |
|---|---|---|---|---|
| static | baseline | 300 | 236.7 ms | 100.0%（纯浪费） |
| static | cached | 5 | 4.0 ms | n/a（零重渲染） |
| moving-light | baseline | 300 | 240.7 ms | 49.1% |
| moving-light | cached | 182 | 114.1 ms | 15.2%（真实变化） |
| material-change | baseline | 300 | 204.0 ms | 99.96% |
| material-change | cached | 15 | 10.1 ms | 98.7%（局部变化） |

- 静止光源：缓存策略把 300 次刷新降到 5 次首帧填充，消除全部无效重渲染。
- 移动光源：太阳 3 级级联逐帧真实失效（未变化比例仅 15%），静止的 lamp
  2 级级联保持缓存（300→182）。
- 材质局部变化：仅含 foliage 投射物的级别失效，20 倍刷新削减，
  重渲染后 98.7% 纹素不变，证实变化是局部的。

完整数据见 `artifacts/summary.csv` 与 `artifacts/report_*.json`
（含每级刷新计数、失效原因矩阵、材质失效明细）。

## 代码结构

- `src/vk_context.*`：Vulkan 实例/设备/提交助手，RAII 图像与缓冲
- `src/spirv_gen.*`：运行时生成深度/可视化着色器的 SPIR-V
- `src/scene.*`：场景、光源、材质版本号、三组验证场景脚本
- `src/shadow_cache.*`：每（光源, 级联）缓存项、失效检测、单级刷新、深度回读
- `src/analyzer.*`：帧驱动、两种刷新策略、前后深度图对比
- `src/report.*`：stdout 表格 + JSON/CSV 报告
- `src/viewer.cpp`：GLFW 交互查看器与离屏 blit 自检
