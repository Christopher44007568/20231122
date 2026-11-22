# 20231122 — GPU 资源生命周期可回放实验

技术栈: C++17 / Vulkan / GLFW(null-platform, 无显示环境也可运行)。

## 一键复现

```bash
./run_experiment.sh   # 构建 -> 执行 -> 回放 -> 顺序比对 -> 生成报告
```

报告见 `artifacts/REPORT.md`, 分“自动结论(机器生成)”与“人工确认(需签字)”两部分;
任一检查失败时报告附带可执行的后续操作, 脚本以非零码退出。

## 实验设计

| 场景 | 目的 |
|---|---|
| `scenarios/baseline.txt` | 完整生命周期基线 |
| `scenarios/cancel_resubmit.txt` | 实验组: 中途取消(丢弃未提交命令)后重录重提 |
| `scenarios/cancel_abort.txt` | 对照组: 中途取消并中止, 验证工作未生效(内容保持 0x00) |
| `scenarios/reorder_a/b.txt` | 顺序改变验证: 最终检查点状态哈希必须一致 |
| `scenarios/repeat.txt` | 重复提交幂等验证 + 重复销毁防御检测 |

## 关键证据(artifacts/)

- `transcript_<场景>.jsonl`: 每一步的输入、状态快照(资源表+计数器+哈希)、结果
- `replay_<场景>.json`: 从转录重新执行的逐步比对判定(mismatches 必须为 0)
- `compare_order.json`: 乱序两场景最终检查点哈希比对
- `REPORT.md`: 自动结论 + 人工确认清单

## 子命令

```bash
./build/gpu_lifecycle run <场景脚本> <转录输出.jsonl>
./build/gpu_lifecycle replay <转录.jsonl> <判定输出.json>
./build/gpu_lifecycle compare <转录A> <标签A> <转录B> <标签B> <判定输出.json>
./build/gpu_lifecycle report <artifacts目录> <场景1,场景2,...>
```

## 依赖

`third_party/` 内置 Vulkan-Headers 与 GLFW 3.4(仅以 null 平台静态构建);
运行时需要 Vulkan loader(`libvulkan.so.1`)与至少一个 ICD(默认使用 Mesa lavapipe 软件设备)。
