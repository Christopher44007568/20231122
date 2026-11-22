# pcdclip — 点云密度裁剪局部修正工具

C++17 / Vulkan / GLFW。对点云做基于局部密度的裁剪修正：针对一组
极限参数，每个参数集复制一个独立的运行对象（run object），可单独
重跑、互相比较、导出；支持中途退出（SIGINT/SIGTERM）与重新进入
续跑；原始材料封存为只读副本；验收锚定到具体 UTC 时间点与原始
偏移（点索引与字节偏移 = index × 16）。

## 构建

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

依赖（Vulkan / GLFW 头文件与库、glslangValidator）默认取自
`third_party/sysroot/usr`（可用 `-DPCDCLIP_SYSROOT=...` 覆盖）。
无显示环境下所有命令均可运行（离屏渲染）；`view` 需要显示服务器。

## 工作流

```sh
pcdclip gen cloud.bin 2000000 11     # 生成带密度异常的合成点云
pcdclip init ws cloud.bin            # 建工作区，封存只读原始材料
pcdclip plan ws                      # 每个极限参数集复制一个运行对象
pcdclip list ws                      # 查看各运行对象状态
pcdclip run ws extreme-loose         # 执行/续跑某个对象（--chunk N）
pcdclip run ws all                   # 依次执行全部对象
pcdclip compare ws extreme-loose extreme-tight   # 比较两个对象
pcdclip compare ws extreme-loose original        # 与原始材料比较
pcdclip export ws extreme-loose out/ # 导出修正结果 + 证据包
pcdclip accept ws extreme-loose      # 验收（时间戳 + 原始偏移）
pcdclip view ws extreme-loose        # GLFW 交互查看（需显示）
```

## 目录结构

```
ws/
  workspace.json            # 清单：创建时间(UTC)、原始 SHA-256、点数
  original/cloud.bin        # 未修改的原始材料（只读，chmod 444）
  original/cloud.sha256
  params.txt                # 极限参数集（set <名> radius=.. min_neighbors=.. max_neighbors=..）
  runs/<id>/
    run.json                # 运行对象：参数、状态、attempt 计数
    working.bin             # 原始材料的独立副本（重复对象）
    checkpoint.json         # 断点：next_chunk / dropped_so_far / 更新时间
    edits.journal           # 追加式编辑日志（16 字节/条：原始索引+动作）
    output.bin              # 修正结果（保留原始索引空间，裁剪位置打标记）
    evidence/
      attempts/<UTC>/       # 每次执行的证据：manifest/metrics/edits.jsonl/
                            # log.txt/render.ppm（含开始/结束 UTC、各 SHA-256）
      acceptance_report.json
```

## 关键行为

- **中途退出**：`Ctrl-C` 在当前 chunk 边界落盘 checkpoint 后以退出码
  2 结束；再次执行同一 `run` 命令从断点续跑，结果与一次性跑完逐位
  一致（journal SHA-256 可证）。
- **单独重跑**：对已完成的对象再次 `run` 会从头重跑并新开一个
  attempt 目录，历史证据全部保留。
- **验收**：`accept` 校验原始材料未被修改、工作副本与原始一致、
  journal 偏移全部落在原始材料范围内且无重复、output 与 journal
  一致、时间线完整有序；报告列出每次 attempt 的起止 UTC 时间点与
  被裁剪点的原始偏移（index 与 byte_offset = index × 16）。

## 实现要点

- 密度估计在 GPU 上完成（Vulkan compute，`shaders/density.comp`）：
  CPU 构建均匀网格（cell ≥ radius，保证 27 邻域搜索正确），compute
  shader 统计每点半径内邻居数，超出 `[min_neighbors, max_neighbors]`
  即标记裁剪。
- 快照为 Vulkan 离屏渲染（`render.ppm`，裁剪点红色）；`view` 为
  GLFW 窗口 + swapchain 的交互旋转视图。
