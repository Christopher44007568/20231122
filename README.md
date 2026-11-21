# rpsc — 可复核的渲染管线状态缓存流程

`rpsc` 把"渲染管线状态缓存"做成一条可复核流水线：**记录 → 截断 → 恢复 →
重放 → 比对**，把 draw call、资源状态（image layout）、帧缓冲哈希和画面差异
逐一对应，并且**指出第一个错误状态**（精确到 op 序号、字段和根因偏移），
而不是只给一个总分。技术栈：C++17 / Vulkan / GLFW。

## 构建

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

依赖（Vulkan headers、GLFW、glslangValidator）已解包在 `.toolchain/` 内，
无需系统安装；运行时使用 lavapipe 软件 Vulkan，GLFW 提供窗口上下文
（离屏渲染，保证 CI/无头环境可复现）。

## 一键复核

```sh
scripts/review.sh            # 构建 + 完整流程 + 自动断言
```

产物在 `artifacts/review/`：

| 文件 | 说明 |
|---|---|
| `master.rpsc2` | 新格式（v2）主记录：长度前缀 + CRC32 + Checkpoint 自校验记录 |
| `legacy.rpsc1` / `legacy_conv.rpsc2` | 旧格式（v1）记录及其 v2 转换（兼容性证据） |
| `truncated.rpsc2` | **被截断的记录**：在第 14 条记录（draw#4 前的 BindPipeline）的 payload 中间切断 |
| `recovered.rpsc2` + `recovery_report.json` | 恢复结果：14/24 条记录被挽救，首个缺陷定位到字节偏移 576（20/40 字节，TruncatedPayload） |
| `golden/state_stream.json` + `golden/draw_*.ppm` | 基准状态流与每帧帧缓冲 |
| `frames_recovered/` | 恢复后重放的帧缓冲转储 |
| `diffs/final_frame_diff.ppm` | 画面差异图：丢失的 3 个三角形一目了然 |
| `replay_recovered.json` / `replay_legacy.json` | 重放比对报告（含 `firstErroneousState` 字段级细节） |
| `REVIEW.md` | 人读复核报告：首个错误状态、根因、draw call 对应表、证据清单 |

## 子命令

```sh
rpsc record --out FILE [--format v1|v2] [--golden DIR]   # 记录场景（v2 注入 Checkpoint）
rpsc convert --in a.rpsc1 --out a.rpsc2                  # 旧格式 -> 新格式
rpsc truncate --in F --out F2 --at-record K              # 制造截断记录
rpsc recover --in F2 --out F3 [--report J]               # 恢复（输出恒为 v2）
rpsc replay --trace F3 --golden G --frames D --diffs E --report J
rpsc review [--out DIR]                                  # 全流程
```

## 格式

- 头部 16B：magic `RPSC`、version、记录数（v1 为未知）、头部 CRC（v2）。
- v1：定长 36B 记录，无校验，无 Checkpoint —— 兼容读取并可无损转 v2。
- v2：`type,flags,len(u16)` + payload + CRC32；Checkpoint 记录携带黄金
  fbHash/stateHash，使记录流可自校验（恢复前缀 0 失配 = 前缀比特级完好）。

## 复核结论（本次运行）

- 第一个错误状态：**有效 op #11，BindPipeline(P3) 缺失** —— 根因是文件偏移
  576 处的记录被截断；后续 3 个 draw call 全部丢失，最终帧缓冲与基准差
  14643 像素（见 `diffs/final_frame_diff.ppm`）。
- 恢复出的前缀与基准逐 op 一致（Checkpoint 0 失配）。
- 旧格式 v1 记录转换后重放与基准零偏差（格式兼容）。
