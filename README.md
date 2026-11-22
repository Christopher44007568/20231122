# Texture Array Paging Debugger

固定技术栈：**C++17 + Vulkan + GLFW**。

该调试器针对纹理数组分页中的两个典型错误：

- 切换页面时，新内容尚未上传完成就提交层映射，导致短暂显示上一页。
- 页表、保留层和最终采样层混用，在层边界或环绕复用时出现索引错位。

当前实现把“保留层”和“可采样层”严格分开。页面只有在 GPU 上传完成后才提交映射；等待期间 `sampleLayer` 返回无效层，片元着色器绘制显式的 `UPLOAD WAIT` 图案，而不是旧页内容。层分配器只复用已经完成且不再被页表引用的层，并在每次采样前检查层号与内容 ID。

![continuous cross-page replay](artifacts/debugger-cross.png)

## 构建与运行

依赖：

- CMake 3.16+
- 支持 C++17 的编译器
- Vulkan loader/headers
- GLFW 3.3+

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/texture_page_debugger
```

无窗口确定性验证：

```bash
./build/texture_page_debugger --headless
./build/texture_page_debugger --headless --summary
./build/texture_page_debugger --scenario cross
ctest --test-dir build --output-on-failure
```

`assets/shaders/*.spv` 已预编译。修改 GLSL 后可用 `glslc --target-env=vulkan1.1` 重新生成。

## 调试器界面

窗口保持固定相机，不响应轨道旋转；回放只推进虚拟页焦点。界面显示：

- **PAGE TABLE**：虚拟页、内容 ID、命中/保留层、状态、命中次数、最近请求序号。
- **FIXED CAMERA**：4x3 虚拟页网格，直接使用 Vulkan `sampler2DArray` 采样已提交层；无效层不会回退到上一页。
- **UPLOAD QUEUE**：上传 ID、页面、目标层、排队/传输状态和剩余 tick。
- **TEXTURE ARRAY LAYERS**：8 个实际数组层的拥有页、内容和共享页数。
- **SAMPLING REQUESTS**：每次逐页回放的请求序号、当前采样层、命中层、最终层和等待时间。
- **PAGE SWITCH TRACE**：要求的切换输出，包含等待、命中层、最终采样层和 `EMPTY`/`DUP` 标记。

控制键：

```text
SPACE        播放 / 暂停逐页回放
LEFT/RIGHT   暂停并前后切换一页
1/2/3        空页 / 重复页 / 连续跨页场景
R            重置当前场景
+/-          调整回放速度
ESC          退出
```

## 验证场景

默认配置为 12 个虚拟页、8 个数组层、2 个上传槽、每页 3 tick 上传延迟（按 60 Hz 显示为 50.0 ms）。

| 场景 | 内容 | 关键断言 |
|---|---|---|
| `empty` | P02、P07 没有源数据，与非空页交替访问 | 空页最终层必须为 `EMPTY`，任何时刻都不能采样旧层 |
| `duplicate` | P04 与 P09 共用内容 C04 | P09 不产生新上传，两个页面命中同一层 |
| `cross` | P00-P11 以 2 tick 间隔连续访问两轮 | 每次最终层都在 `[0, 7]`，达到 L07 并发生层复用 |

每个切换事件输出：

```text
seq=003 page=P03 content=C03 request_tick=12 resolve_tick=15 wait=3t/50.0ms \
hit_layer=-- final_sample_layer=L01 outcome=WAIT empty=no duplicate=no
```

字段含义：

- `wait`：从请求到页面映射可采样的时间，同时输出 tick 和毫秒。
- `hit_layer`：请求瞬间已经命中的层；未命中或空页为 `--`。
- `final_sample_layer`：等待结束后实际提交给着色器的层；空页为 `EMPTY`。
- `outcome`：`HIT`、`DUP`、`WAIT` 或 `EMPTY`。

## 关键实现

- `src/PagingModel.*`：确定性页表、LRU 层分配、上传槽、重复内容共享、事件记录和不变式检查。
- `src/Scenarios.*`：三种回放场景及无窗口验证报告。
- `src/DebugUI.*`：CPU 软件光栅化调试面板，逐帧上传为 Vulkan UI 纹理。
- `src/VulkanApp.*`：GLFW 窗口、Vulkan 交换链、纹理数组、上传提交、固定相机网格绘制和输入回放。
- `assets/shaders/`：真实 `sampler2DArray` 页面采样和透明 UI 合成着色器。

上传开始时先等待 GPU 空闲，再改写目标层，避免飞行中的帧继续采到被复用的旧内容；映射提交发生在上传完成之后。因此页切换不会先显示上一页，层索引也不会在 `L00`/`L07` 边界处越界或错位。
