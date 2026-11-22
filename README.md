# csm_replay_lab — 阴影级联可视化可回放实验

技术栈: C++17 / Vulkan / GLFW。渲染 3 级联 CSM(Cascaded Shadow Maps)
并以红/绿/蓝色调可视化级联分布,围绕"记录 → 快照 → 恢复 → 重放 →
乱序 → 重复提交"构建可回放实验,逐步记录输入、状态哈希与结果。

## 构建

```sh
cmake -S . -B build && cmake --build build -j
```

工具链自包含于 `third_party/`(Vulkan 头文件、GLFW 3.4);着色器由本机
`glslc` 在构建期编译为 SPIR-V。运行时使用系统 Vulkan loader(本机为
llvmpipe 软件光栅化,结果逐位确定)。

## 运行

```sh
./build/csm_lab run                 # 完整实验,退出码 0=全部自动检查通过
./build/csm_lab present             # GLFW 开窗展示级联可视化
./build/csm_lab help
```

## 实验步骤

1. `record/glfw_probe` — 记录 GLFW 所需实例扩展(环境证据)
2. `record/vk_init_control` — 未知扩展对照组:探测
   `VK_EXT_csm_lab_unknown_control` / `VK_EXT_csm_lab_unknown_device_control`,
   验证带未知扩展的 `vkCreateDevice` 返回 `VK_ERROR_EXTENSION_NOT_PRESENT`
3. `record/renderer_init` / `record/render_baseline` — 基线渲染 + SHA-256
4. `record/snapshot` — 参数、基线哈希、状态哈希写入 `snapshot.state`
5. `replay/*` — 销毁上下文,从快照恢复,重放渲染,验证逐位一致
6. `reorder/cascade_order_permuted` — 级联渲染顺序 [0,1,2]→[2,0,1],
   验证独立步骤顺序无关
7. `reorder/order_negative_*` — 负对照:先用另一光源方向填充阴影图,
   再"先场景后阴影",验证依赖步骤顺序敏感(结果必须不同)
8. `duplicate/duplicate_submit` — 同一命令缓冲重复提交两次,验证幂等

## 产物(artifacts/)

- `trace.jsonl` — 每步的输入 / 状态哈希 / 结果(JSONL)
- `snapshot.state` — 快照(参数 + 基线哈希 + 状态哈希)
- `hashes.json` — 全部证据图像的 SHA-256
- `*.png` — 关键证据图像(基线 / 重放 / 乱序 / 负对照 / 重复提交)
- `report.md` — 报告:**自动结论**(机器判定表)与**人工确认**
  (人工核对清单)分开呈现;任一检查失败时附**可执行的后续操作**
