# 体素网格切片 · 可复核流程

本流程把同一场景的两个版本快照（旧格式 `VOX1` 与新格式 `VOX2`）渲染为逐
z 切片画面，并把 **draw call、资源状态、帧缓冲布局、画面差异** 串成一条带
单调序号与时间戳的证据链，供事后复核。

## 1. 数据格式

多字节整数一律小端、紧凑无填充。

- `VOX1`（旧）：`magic("VOX1") | dimx | dimy | dimz | voxel[N]`，
  每体素 1 字节，索引 `x + dimx*(y + dimy*z)`。
- `VOX2`（新）：在头部后增加 `metadata_length | metadata[]`，体素改为
  y-major 排列 `x + dimx*(z + dimz*y)`；加载后由 `normalize_layout()`
  统一回规范索引。

截断（payload 短于头部承诺）不抛异常：`Snapshot.truncated=true`，保留
`expected_bytes/observed_bytes`，缺失单元按 0 渲染，流程继续处理其余对象。
结构性错误（缺文件、坏 magic、头不可读）才抛 `SlicerError`，由对象级
try/catch 隔离，不影响后续对象。

## 2. 渲染与证据

每个快照独立建立并销毁一组 Vulkan 资源（2D 数组纹理、视图、采样器、
描述符集、管线布局/管线、颜色 image、framebuffer、staging buffer）。
证据写入 `<out>/pipeline.jsonl`（JSON Lines，每行一条）：

- `snapshot_render_begin/end`：格式、截断、承诺/实读字节。
- `resource_state`：纹理最终布局 `SHADER_READ_ONLY_OPTIMAL` 与层数。
- `draw_call`：切片号、顶点/实例数、纹理布局、帧缓冲布局前/中/后、后端。
- `framebuffer_readback`：读回尺寸与像素指纹（双 64 位 FNV 哈希）。
- `frame_diff`：两版本同一切片的差异像素数、最大通道差、千分制平均绝对
  误差、双方指纹。
- `pipeline_fallback` / `pipeline_ready`：实际使用的后端与原因。
- `resource_cleanup`：该快照资源已释放。
- `object_error`：对象级异常信息（其余对象继续）。

画面以 PPM(P6) 落盘：`legacy_v1/`、`modern_v2/`、`diff/`（差异掩码为
品红）。`review_summary.json` 汇总截断记录数与每个对象的状态。

## 3. 后端与回退

优先走完整 Vulkan 图形管线（render pass + pipeline + `vkCmdDraw`）。
当 `vkCreateGraphicsPipelines` 失败时（见下节环境说明），回退到 CPU
栅格化，但仍把像素写进同一个 Vulkan 颜色 image、经相同的布局转换与
Vulkan 读回路径产出证据，因此帧缓冲/状态/指纹字段在两条路径下口径一致，
且每次回退都有 `pipeline_fallback` 留痕。

## 4. 环境说明（lavapipe）

在本机（Mesa 26.0.8 + LLVM 21.1.8，WSL2 内核 6.18）上，lavapipe 的
SPIR-V 管线编译对任何着色器（含规范参考模块、空 compute 模块）均返回
`VK_ERROR_UNKNOWN(-13)`；而 OpenGL llvmpipe 的着色器编译、Vulkan 的
image/buffer/命令提交/读回均正常。该缺陷属驱动后端，与本工程的 SPIR-V
无关，故采用上述带证据的 CPU 回退保证流程可运行、可复核。

## 5. 复现

```sh
cmake -S . -B build
cmake --build build -j4
./build/make_fixtures data/input
./build/voxel_slicer data/input evidence_out
```

着色器以 `src/shaders/*.spvasm`（可读汇编）为源，由无依赖的
`tools/spvasm.py` 在构建时生成 SPIR-V 头；仓库内同时提交生成结果作为
离线兜底。两次运行的 PPM 与指纹逐字节一致（可复现）。
