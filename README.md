# 20231122

## texarray-review — 可复核的纹理数组分页流程

C++17 / Vulkan / GLFW 工具，围绕一个未知扩展
（`VK_EXT_texarray_paging_emulated`，模拟）记录 **两个版本快照**，并把
draw call、资源状态、帧缓冲与画面差异逐一对应起来。

### 流程

1. `capture v1`：基线分页策略（LRU、同步换入），写 **旧格式 TAPS 1** 快照。
2. `capture v2`：扩展路径（投机分页：缺页当帧绘制占位图案、下一帧提升，
   FIFO 换出），写 **新格式 TAPS 2** 快照。
3. 兼容读取器按 `TAPS <n>` 头自动分发，v1/v2 解析到统一内存模型；
   未知记录与坏行只产生告警，不中断解析。
4. `Correlate` 逐帧比对帧缓冲 PNG，生成差异热图，并按 draw call 的
   帧缓冲区域把差异归因到对象 / 页 / 层 / 缺页事件。
5. 证据保留在 `artifacts/evidence/`（快照、帧 PNG、差异 PNG、
   `report.md` / `report.json`）；临时目录 `artifacts/tmp/` 在结束时
   自动清理（RAII，异常路径同样生效）。
6. 逐对象 / 逐帧 / 逐记录异常隔离：单个对象失败被记录为 `error` 事件，
   其余对象继续处理（`--inject-fault` 可复现）。

### 构建与运行

```sh
scripts/bootstrap_deps.sh            # 安装 Vulkan-Headers / GLFW / glslang 到 ~/deps
cmake -B build -DDEPS_ROOT=~/deps -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/texarray-review review --out artifacts      # 完整复核流程
./build/texarray-review selftest --out artifacts    # 含故障注入与清理断言的自检
```

无显示服务器时 GLFW 使用 Null 平台，渲染完全离屏（llvmpipe 即可运行）。

### 子命令

- `review`：capture(v1, TAPS1) + capture(v2, TAPS2) + 兼容解析 + 差异关联 + 报告 + 清理。
- `capture --version v1|v2 --snapshot FILE --evidence DIR [--format 1|2] [--inject-fault i,j]`：单次采集。
- `diff --a SNAP --b SNAP --evidence DIR`：对任意两个版本的快照做关联比对。
- `selftest`：干净跑一遍，再以 `--inject-fault 3,7` 跑一遍，校验错误事件数、
  draw call 数与临时目录清理。

### 快照格式

- **TAPS 1（旧）**：`meta` / `draw` / `resource` / `framebuffer` 行，
  `draw` 无 `page/set/binding/placeholder` 字段（读取时 `page` 回退为 `layer`）。
- **TAPS 2（新）**：增加 `event`（pagefault / error）、`draw` 扩展字段、
  `resource` 的 `evicted`/`membytes`、`meta` 的 `extension`/`enabled`。
