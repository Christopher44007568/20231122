# GPU 时间戳采样 · 来源检查页

C++17 + Vulkan + GLFW 实现的 GPU 时间戳采样来源检查工具。一次运行完整演示:

1. **实时采样** — `vkCmdWriteTimestamp` 夹住 GPU 拷贝工作,逐帧追加到带 FNV-1a-64 哈希链的 JSONL 日志;
2. **崩溃截断** — 指定帧的记录只写入部分字节即"崩溃",留下一条被截断的记录;
3. **恢复重放** — 扫描定位首个坏尾、打捞原始字节存证、截断日志到最后一条已提交记录,然后重放丢失帧并以 `origin=replay` 回链截断事件;
4. **分析判定** — 按字节流顺序逐条检查(可用性 → 单调性 → 预算),给出**第一个错误状态**而非总分;结束时间戳不可用的帧被判定为**无法判断(UNDECIDABLE)**边界,原始证据保留;
5. **检查页** — 生成自包含 HTML,每条结论都可点击回到原始输入字节和处理步骤。

## 构建

```sh
cmake -S . -B build && cmake --build build
```

依赖已 vendored 在 `third_party/`(Vulkan 头文件、GLFW 3.4);运行时链接系统 `libvulkan.so.1`(本机为 lavapipe/llvmpipe 软件设备)。

## 运行

```sh
./build/gpu_ts_inspect --out artifacts \
    --frames 8 --crash-at 5 --lost-end 6 --budget-ns 100000000
```

- `--crash-at N`:第 N 帧的记录写到一半即崩溃(产生被截断的记录)
- `--lost-end N`:第 N 帧不写结束时间戳(可用性位为 0,构成无法判断的边界)
- `--budget-ns N`:每帧时长预算

## 产物(`artifacts/`)

- `report.html` — 来源检查页(首个错误状态横幅、结论表、原始输入、处理步骤、关键证据)
- `gpu_ts.log` — 恢复后的哈希链日志(每行一条合法 JSON 记录)
- `evidence/truncated_tail.bin` — 崩溃现场打捞的截断字节
- `evidence/truncation_event.json` — 截断事件(偏移、原因、打捞哈希)
