# GS Output Checker (gsochecker)

几何着色器输出检查器。把 GS 每个 invocation 实际写出的顶点捕获到
host-visible SSBO，逐项列出输入顶点数、生成图元数、`max_vertices` 上限与
实际缓冲写入，并支持暂停在单个 invocation 查看其完整输出。

技术栈：C++17 / Vulkan / GLFW（GLFW 使用 Null 平台，可无窗口系统运行；
渲染走离屏 render pass + rasterizer discard，无需 swapchain）。

## 构建

```sh
cmake -S . -B build && cmake --build build -j
```

依赖已 vendored 在 `third_party/`（Vulkan 头文件 + glslc 由 deb 解包，
GLFW 3.4 源码构建）。若系统装有 glslc / Vulkan SDK 也可直接使用。

## 运行

```sh
./build/gsochecker                      # 全部拓扑 x 三组样例，结束后暂停在首个异常 invocation
./build/gsochecker --topo triangles     # 只跑三角形
./build/gsochecker --emit 300           # 自定义每 invocation 发射顶点数
./build/gsochecker --inspect 2          # 暂停在 invocation 2，逐条 dump 输出记录
./build/gsochecker --non-interactive    # 不等待按键，直接退出（CI 用）
```

## 验证样例

内置三组样例（`max_vertices = 8`，4 个 GS invocation）：

- `zero-output`：emit = 0，期望 captured = 0；
- `at-limit`：emit = 8 = max_vertices，期望 captured = 8；
- `over-limit`：emit = 11 > max_vertices，检查器报告第一个异常
  invocation，并提示超出上限的图元在下游会被丢弃/行为未定义。

每组样例输出 per-invocation 表格（输入顶点 / 请求发射 / 上限 / 实际捕获 /
生成图元）与汇总（总发射、总图元、丢弃记录数、缓冲占用字节与百分比）。
缓冲写满时报告丢弃总数与首次丢弃的 invocation。

## 实现要点

- `shaders/capture.geom`：按 `-DTOPO=0/1/2` 编译出 points / line_strip /
  triangle_strip 三个变体；每次 `EmitVertex` 前原子自增计数器并写入
  `{invocation, seq, inputVerts, pos}` 记录，写不下时计入 dropped 并用
  `atomicMin` 记录首个丢弃的 invocation。
- `src/main.cpp`：Vulkan 实例/设备（启用 `geometryShader`）、无附件
  render pass（`rasterizerDiscardEnable = VK_TRUE`）、host-visible
  捕获缓冲、CPU 侧分析与报告。
