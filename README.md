# 20231122

## 抗锯齿历史验证 · 来源检查页

C++17 + Vulkan + GLFW 实现的来源检查工具：探测 Vulkan 实例/设备信息，
生成每条结论均可回溯到原始输入与处理步骤的 HTML 报告。

处理对象：
- 版本快照 A（实例级，`vkEnumerateInstanceVersion`）
- 版本快照 B（设备级，`VkPhysicalDeviceProperties`），与 A 对比
- 未知扩展（与内置已知注册表比对，全部已知时注入合成探针）
- 抗锯齿边界（请求采样数 3，非 2 的幂 → 无法判断，保留原始位掩码）

单个对象异常不中断其余对象；结束时清理临时目录与 Vulkan/GLFW 状态。

构建与运行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aa_history_verify
# 输出 artifacts/aa_history_report.html 与 artifacts/evidence.json
```
