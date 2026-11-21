# 20231122

# GPU 骨骼动画蒙皮 · 可复核流程

技术栈固定为 **C++17 + Vulkan + GLFW**。对每个输入对象录制两个版本快照，
把 draw call、资源状态、帧缓冲和画面差异逐项对应，保留证据并在结束时清理
临时状态；单个对象出错不会中断其余对象的处理。

## 复核流程

1. **扫描输入目录**：按文件名排序逐个处理，不依赖扩展名判断格式。
2. **格式识别**：读取文件头 magic，分派到旧版 v1 或新版 v2 解析器；
   无法识别的 magic/扩展名抛 `FormatError`，记录后跳过，继续下一个对象。
3. **双版本快照**：
   - 快照 A：按文件原始修订版渲染。
   - 快照 B：把同一逻辑对象转存为另一修订版后再次渲染。
4. **GPU 蒙皮渲染**：离屏帧缓冲（color + depth），皮肤矩阵经 UBO 传入，
   顶点着色器内做 4 骨骼加权蒙皮，单个 `vkCmdDrawIndexed`。
5. **证据采集**：draw call 参数、顶点/索引绑定、皮肤矩阵数量、有序的
   image layout 转换、帧缓冲格式，以及 RGBA 读回像素。
6. **关联与判定**：比对两版画面，叠加 CPU 参考蒙皮交叉校验，输出结论。
7. **清理**：转存用的临时文件放在输出目录的 `.tmp/` 下，由 RAII 清理器
   在进程退出前无条件删除。

## 两种容器格式

| 维度 | 旧版 Legacy v1 (`SK1\0`) | 新版 Current v2 (`SK2\0`) |
| --- | --- | --- |
| 骨骼权重 | `uint16` 量化（/65535 后重归一化） | `float` 归一化权重 |
| 矩阵 | 行主序 3x4，隐式末行 | 列主序 4x4 |
| 骨架 | 全局绑定表 + parent 索引 | 每骨骼 inverse-bind + parent 层级 |
| 名称 | 定长字符数组 | 长度前缀字符串 |

## 证据布局

```
review_output/
  manifest.json                 # 批处理索引：每对象 ok/error/证据路径
  <object>/
    snapshots.json              # 两个快照的完整结构化记录
    correlation.json            # 关联报告（draw call/状态/帧缓冲/画面）
    legacy_v1.ppm               # 旧版快照画面
    current_v2.ppm              # 新版快照画面
    diff.ppm                    # 三联图：旧版 | 新版 | 8x 放大差异
```

`correlation.json` 中的判定字段：

- `verdict`：`EXACT_MATCH` / `EQUIVALENT_WITHIN_TOLERANCE` / `DIVERGENT`
- `deltaAttribution`：差异归因（v1 权重 uint16 量化导致的边缘像素差异）
- `cpuGpuSkinningConsistent`：CPU 标量参考实现与 GPU 蒙皮数学一致性
- `imageDelta`：差异像素数、平均绝对误差、最大通道误差

## 构建与运行

无 root 环境下，依赖放在 `third_party/`（Vulkan-Headers、GLFW null 平台
静态库、glslang）。系统需提供 Vulkan loader（`libvulkan.so.1`）与
lavapipe 驱动（Mesa）。

```bash
./run_review.sh
```

等价的手动步骤：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/generate_sample sample_input
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
    build/skinning_review sample_input review_output
ctest --test-dir build
```

GLFW 在 null 平台初始化（无窗口、无 surface、无 swapchain），渲染目标是
纯离屏帧缓冲，因此无显示环境也可运行。PPM 可用 `ffmpeg`、ImageMagick 等
任意工具转 PNG 复核。

## 退出码

- `0`：所有可识别对象复核通过（未知格式对象被隔离跳过）。
- `1`：存在复核失败的对象。
- `2`：输入目录不存在。
- `3`：Vulkan 初始化等致命错误。

## 源码地图

- `src/skinning_format.*`：v1/v2 二进制读写与 `FormatError`
- `src/skeleton.*`：层级变换链、皮肤矩阵、CPU 参考蒙皮
- `src/vk_common.*`：GLFW/Vulkan 上下文、设备选择、命令缓冲
- `src/renderer.*`：离屏帧缓冲、蒙皮管线、屏障记录与像素读回
- `src/evidence.*`：PPM 输出、画面差异、JSON 证据
- `src/cpu_check.*`：CPU/GPU 蒙皮一致性交叉校验
- `src/main.cpp`：批处理驱动、双快照编排、异常隔离、清理
- `tools/generate_sample.cpp`：双格式样例与未知扩展坏文件生成
- `tests/`：无 GPU 依赖的格式往返与数学回归测试
