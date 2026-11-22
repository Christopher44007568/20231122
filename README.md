# 体素网格切片 · 可复核渲染流程

技术栈固定为 **C++17 + Vulkan + GLFW**。围绕两个版本快照（旧 `VOX1` /
新 `VOX2`）把每个体素对象逐 z 切片渲染，并将 draw call、资源状态、帧缓冲
布局与画面差异对应成带序号的 JSONL 证据；兼容新旧格式、保留截断记录、
单对象异常不影响其余对象、结束即清理全部临时 GPU 状态。

## 快速开始

```sh
cmake -S . -B build
cmake --build build -j4
./build/make_fixtures data/input
./build/voxel_slicer data/input evidence_out
```

输出：

- `evidence_out/pipeline.jsonl`：全流程结构化事件（draw call / 状态 / 读回 / 差异）。
- `evidence_out/<obj>/{legacy_v1,modern_v2,diff}/slice_*.ppm`：逐切片画面与差异掩码。
- `evidence_out/review_summary.json`：截断记录数与各对象状态汇总。

## 目录

- `src/`：主流程、体素 IO、Vulkan 上下文、切片渲染器、证据写入。
- `src/shaders/`：SPIR-V 可读汇编源（`.spvasm`）与生成的头文件。
- `tools/spvasm.py`：无依赖的最小 SPIR-V 汇编器（构建时调用）。
- `tools/make_fixtures.cpp`：生成含截断记录与损坏文件的测试夹具。
- `docs/REVIEWABLE_PIPELINE.md`：格式、证据字段、回退与环境说明、复核步骤。

## 设计要点

- 截断记录不致命：记录承诺/实读字节并继续；结构错误才抛异常且按对象隔离。
- 资源按快照 RAII 管理，异常路径也会释放；临时读回 buffer 即用即销。
- 结果可复现：相同输入两次运行的 PPM 与像素指纹逐字节一致。
- 当本机 lavapipe 无法编译 SPIR-V 管线时，自动回退 CPU 栅格化，但 Vulkan
  资源编排、布局转换与读回保持不变，并在证据中显式标注回退。
