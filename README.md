# Stable Volumetric Fog Sampling

一个固定使用 **C++17 + Vulkan + GLFW** 的体积雾暂停/恢复示例。程序围绕一个故意缺失的密度片段运行极限参数场景，保存完整中间状态，恢复后重放同一采样边界并继续积分，同时输出可校验的证据。

## 构建

依赖：

- CMake 3.16+
- C++17 编译器
- Vulkan loader/headers
- GLFW 3.3+

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

仓库已包含校验过的 SPIR-V。修改 GLSL 后可用 `glslangValidator` 重新生成：

```bash
GLSLANG_VALIDATOR=/path/to/glslangValidator ./scripts/compile_shaders.sh
```

## 运行

桌面环境下交互运行：

```bash
./build/fog_stability
```

无头证据流程（适用于 CI / 无显示服务器环境）：

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
xvfb-run -a ./build/fog_stability --headless \
  --config configs/missing_fragment_stress.ini \
  --evidence artifacts/evidence
```

强制执行低显存降级路径：

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
xvfb-run -a ./build/fog_stability --headless \
  --simulate-vram-mb 64 \
  --frames 8 --continue 2 \
  --evidence artifacts/evidence-degraded
```

只从已有检查点恢复：

```bash
xvfb-run -a ./build/fog_stability --resume artifacts/evidence/checkpoint.fogcp \
  --evidence artifacts/evidence-resume
```

交互按键：`P` / `Space` 在采样边界暂停或恢复，`Esc` 退出。

## 稳定化流程

- 计算着色器每帧使用 `frameIndex` 驱动确定性抖动和雾场漂移。
- RGBA16F 乒乓图像保存时间累积结果与前序样本数。
- 暂停时写出 `nextFrame`、完整 `FogUniforms`、RGBA16F 历史图像和 FNV-1a 64 校验值。
- 恢复时重新上传历史图像，并以检查点内的 `FogUniforms` 继续，不依赖启动配置；重放同一帧验证逐位一致后再继续处理。
- 检查点文件写入使用临时文件加重命名，并校验尺寸、magic、版本、哈希。
- 缺失密度片段命中后使用低频保守回退值，GPU 原子计数记录命中次数；NaN/Inf 被拒绝并计数。
- 边界前保存历史帧样本，边界后保存首个恢复帧样本；同时保存整幅图的平均绝对差、RMSE、最大差、变化像素数和逐位重放结果。

## 资源不足与降级

渲染器在创建资源前估算显存，并按三级计划尝试：

- `full`：配置分辨率与步数。
- `balanced`：75% 分辨率、75% 步数。
- `safe`：50% 分辨率、50% 步数。

预算不足或真实 `VkAllocateMemory` 失败都会回退到下一级；最低级仍失败则终止并报告原因。`--simulate-vram-mb` 可复现预算不足路径，降级原因会写入证据。

## 关键证据

默认输出到 `artifacts/evidence`：

- `run.json`：设备、参数、资源计划、降级原因、检查点哈希、边界样本和差异指标。
- `summary.txt`：便于快速 grep 的 `key=value` 摘要。
- `checkpoint.fogcp`：可独立恢复的二进制中间状态。
- `boundary_before.ppm` / `boundary_after.ppm`：边界前后可视化。
- `boundary_before.rgba16` / `boundary_after.rgba16`：无损线性采样结果。
- `boundary_replay.ppm`：同一检查点重放帧。
- `continuation.ppm`：恢复后继续积分的结果。

极限场景位于 `configs/missing_fragment_stress.ini`；核心实现位于 `src/renderer.cpp`、`src/checkpoint.cpp` 和 `shaders/fog.comp`。
