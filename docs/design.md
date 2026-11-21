# 设计说明

## 状态机

事务合法路径：

```text
submit -> validate -> upload_begin -> upload_complete -> commit
```

任一未提交阶段可 `cancel`。取消清除 pending 事务和暂存 GPU 资源，但从不修改
`MaterialState.active_*`。已提交事务的取消返回 `too_late`；已取消事务再次取消
返回 `already_canceled`。

幂等键作用域为 `material + key`。相同 key 与相同请求哈希返回原事务号并标记
`duplicate_accepted`；相同 key 的不同载荷返回 `idempotency_conflict`。

## Vulkan 上传

`upload_begin` 为事务创建：

- host-visible/coherent staging buffer；
- device-local buffer；
- host-visible readback buffer；
- command buffer 与 fence。

命令缓冲执行 `staging -> device -> readback` 拷贝及 Transfer→Host 屏障后提交到
图形/计算/传输队列。`upload_complete` 等待 fence 并比较读回字节。
`commit` 仅在校验通过后晋升 device buffer；`cancel` 等待 fence、销毁全部暂存
对象并释放事务。

## 回放

原始运行和回放共用同一命令执行函数。回放使用相同初始状态、相同命令顺序、
相同链种子 `mi-hotupdate-chain/v1`，逐步比较规范化核心 JSON 和事件哈希。
独立 `--replay` 模式读取 `commands.jsonl`，再与相邻 `events.jsonl` 的
`event_hash` 序列比较。

后端信息不进入事件哈希，因此 `fake` 与 `vulkan` 后端应产生相同的状态和事件链。
