# 转储输入格式（.gcdump）

来源检查器只接受基于行的纯文本，确保每一条输入都有稳定的 1 起始行号，
行号是整条溯源链的锚点，永远不会被重写或重新编号。

## 语法

| 写法 | 含义 |
| --- | --- |
| `# 任意注释` | 保留为原始行，不产生字段 |
| `key: value` | 一条字段记录，以第一个冒号分隔 |
| 空行 | 仅保留为原始行 |
| 不含冒号的行 | 解析错误 `E1001`，触发失败 `F1001` |

未知的 `key` 不会被拒绝：例如未知扩展必须原样保留，才能端到端审计。

## 保留字段

| 字段 | 用途 |
| --- | --- |
| `dump_id` / `device_name` / `driver_version` | 身份信息，缺失触发 `F2002` |
| `checksum_expected` / `checksum_actual` | 字节完整性，比对失败触发 `F2004` |
| `extension: <name> status=<token>` | 扩展记录，每行一条 |
| `replay_status` | 恢复后重放状态，缺失触发 `F2003` |
| `replay_frames_total` / `replay_frames_replayed` | 重放覆盖率 |
| `replay_diverged_frame` | 首个分歧帧，`none` 表示无分歧 |

## 自动判定规则

- 校验和相等 → `AUTO_CONFIRMED`；不等 → `AUTO_REJECTED` 且阻塞。
- 重放 `completed`、帧数全覆盖、无分歧帧 → `AUTO_CONFIRMED`。
- 存在分歧帧或帧数不一致 → `NEEDS_MANUAL_REVIEW`，并记录边界行。
- 内建知识库中的扩展按规范化 `status` 自动确认/拒绝。
- 知识库之外的扩展 → `INDETERMINATE`，检查器不得猜测，只保留证据并
  生成人工确认项与可执行后续操作。
