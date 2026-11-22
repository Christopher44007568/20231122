# topo_diag — 图元拓扑诊断（可复核流程）

以一次**恢复后的重放**为主线的 Vulkan 图元拓扑诊断工具。技术栈固定为
C++17 / Vulkan / GLFW，全部渲染离屏进行，可在无显示环境（lavapipe）运行。

## 构建

```sh
# 依赖见 third_party/README.md（已随环境预置）
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

无头运行时需要：

```sh
export LD_LIBRARY_PATH=third_party/sysroot/usr/lib/x86_64-linux-gnu
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json   # 软件 Vulkan
```

## 可复核流程（主线）

1. **录制**：`topo_diag record --out good.ptdc`
   渲染演示场景（4 个不同拓扑的 draw call），把 draw call、资源状态时间线、
   帧缓冲参考画面和**一个未知扩展记录**（`VK_EXT_unknown_topology_probe`）
   一并写入捕获。
2. **模拟恢复损坏**（演示用）：`topo_diag record --out recovered.ptdc
   --corrupt-topology 1 TRIANGLE_LIST`
   参考画面仍由正确拓扑生成，但文件中 draw 1 的拓扑被改写——等价于一次
   恢复后状态不一致的捕获。
3. **重放诊断**：`topo_diag replay recovered.ptdc`
   自动完成：加载（v1/v2 自适应）→ 未知扩展标记与存证 → 逐 draw 检查点
   重放 → 帧缓冲读回 → 与参考画面逐像素对比 → 把差异关联回 draw call
   （绘制区域命中 + 顶点包围盒命中双信号）→ 资源状态核对 → 出报告。
4. **假设验证（闭环）**：报告会给出可直接执行的后续命令，例如
   `topo_diag replay recovered.ptdc --override-topology 1=TRIANGLE_STRIP`，
   重放后差异消失即确认拓扑恢复错误。
5. **人工确认**：报告的"人工确认"一节为勾选清单，与"自动结论"严格分节，
   由人复核证据后回填。

## 捕获格式（PTDC）

- **v1（旧格式）**：draw 记录 + 顶点数据 + 参考画面哈希。重放时只能做整体
  哈希校验，报告会注明该限制并建议升级重录。
- **v2（新格式）**：扩展记录、资源状态时间线、draw→资源引用、完整参考画面。
  加载器自动识别版本并把 v1 升级到统一的内存模型。

## 报告与证据

每次重放在 `evidence/<UTC 时间戳>/` 下保留关键证据：原始捕获副本、
扩展记录、逐 draw 帧缓冲检查点（PPM）、重放最终帧、参考画面、差异图，
以及 `report.md` / `report.json`。报告分节：**上下文 / 自动结论 /
人工确认 / 后续操作 / 证据清单**（含 FNV-1a 校验值）。

退出码：`0` 无错误级结论；`2` 存在错误级结论（见报告）；`1` 工具自身错误。

## 测试

```sh
ctest --test-dir build --output-on-failure
```

覆盖：录制（v1/v2/损坏）→ 干净重放 → 损坏重放定位 draw 1 → 假设覆盖后
差异消失 → 旧格式哈希校验 → 报告自动/人工分节。
