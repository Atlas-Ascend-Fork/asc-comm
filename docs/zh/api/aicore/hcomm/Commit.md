# Commit

## 功能说明

通知指定普通通道上已准备的通信任务可以开始执行。通常在调用普通`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`或`AtomicCAS`时将`commit`设置为`false`后显式调用。

该接口只接受`ChannelHandle`。BatchHandle中的任务需要通过[BatchCommit](./BatchCommit.md)提交。

## 函数原型

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 普通通信通道句柄。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `pipe` | commit使用的pipe，默认`PIPE_S`。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前需要通过`Init`为普通接口提供临时工作区。
- 若`commit`模板参数设为`false`，连续积攒的任务槽位不得超过通道的任务提交容量；需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录。对于URMA任务，`config`是传入各通信接口的任务配置，其`cqe`字段用于控制该任务是否生成完成记录。中间任务的`config.cqe`设为0，最后一个任务设为1。
- BatchHandle中的任务只能通过`BatchCommit`提交，不能在同一通道上混用普通`Commit`和`BatchCommit`。
