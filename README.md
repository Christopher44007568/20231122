# 实时反射探针（Reflection Probe）— 可复核录制/重放流程

C++17 + Vulkan + GLFW 实现的反射探针捕获、截断恢复与重放复核工具。
场景：主通道地板采样一张实时反射纹理（planar reflection probe），
每一次 draw call、资源状态迁移、帧缓冲哈希都被写入探针记录。

## 构建

依赖已解包到 `third_party/prefix`（vulkan-headers、GLFW 3.4、glslangValidator），
运行时使用系统 lavapipe（llvmpipe），无显示器也可跑（GLFW null platform）。

```sh
cmake -S . -B build && cmake --build build -j
```

## 一键复核流程

```sh
./scripts/run_review.sh
```

流程：录制 v1/v2 参考捕获 → 生成截断记录 → 恢复 → 重放 → 复核，
证据写入 `artifacts/evidence/`（`reference.png` / `replay.png` /
`diff.png` / `report.json`），记录文件在 `artifacts/traces/`。

## 记录格式（向后兼容）

- **v1（旧格式）**：仅 draw call 记录（名称、管线、顶点数、帧缓冲目标、
  状态哈希），无 CRC、无帧缓冲哈希。读取器自动识别并按“仅状态”模式复核。
- **v2（新格式）**：增加资源状态迁移记录（如 `reflectionTex`
  `COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL`）、每条记录的
  CRC32、每次 draw 后的帧缓冲哈希，以及含记录总数与最终帧哈希的页脚。

## 截断与恢复

`probe truncate` 模拟探针进程崩溃：保留前 K 条完整记录 + 下一条记录的前
N 个字节，无页脚。`probe recover` 顺序扫描并校验每条记录的 CRC，
抢救出全部完整记录，报告截断点（第一条丢失记录的步序号）并重建合法文件。

## 复核（review）

`probe review --reference 完整记录 --candidate 截断记录` 会：

1. 严格解析参考记录，以恢复模式解析候选记录；
2. 在全新 Vulkan 实例上按候选记录重放 draw call，逐 draw 计算帧缓冲哈希；
3. 把 draw call ↔ 资源状态 ↔ 帧缓冲哈希 ↔ 最终画面差异逐条对应；
4. **指出第一个错误状态**（`firstError`：步序号 + 类型 + 明细），
   而不是只给一个总分；退出码 2 表示发现分歧。

当前证据：截断丢失了第 7 步 `main/objectA` 起的主通道绘制，
`diff.png` 中两个三角形区域即为丢失的 draw call 造成的画面差异。
