# Commit

## 功能说明

通知指定普通通道上已准备的通信任务可以开始执行。通常在调用普通`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`或`AtomicCAS`时将`commit`设置为`false`后显式调用。

该接口只接受`ChannelHandle`，不提交BatchHandle中的WQE。批量接口需要调用[BatchCommit](./BatchCommit.md)。

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
- BatchHandle中的WQE只能通过`BatchCommit`提交。普通`Commit`和`BatchCommit`使用不同的队列状态管理方式，不能在同一通道上混用。
