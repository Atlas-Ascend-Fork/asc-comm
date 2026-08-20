# BatchCommit

## 功能说明

将批量句柄中已准备的WQE从UB缓冲区复制到通道GM SQ，并更新队列计数、敲SQ doorbell，使本批次任务开始执行。

当前该接口仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

## 函数原型

```cpp
template <
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | 由`MakeBatchHandle`创建的批量句柄。成功提交后，本地`cqHead`同步到通道，句柄中的本批次WQEBB计数清零。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导；当前支持`UbcCtpBatchHandle`。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败，包括空批次、WQEBB计数异常或批次WQEBB数量不满足SQ容量约束。 |

## 约束说明

- 调用前需要通过批量`ReadNbi`、`WriteNbi`或`WriteWithNotifyNbi`至少准备一个WQE。
- 一个批次内可以混合准备批量读、写和写通知任务。
- 本批次已准备的WQEBB数量不能超过BatchHandle的UB缓冲区容量，并且必须严格小于`sqDepth`。
- 接口支持SQ环回场景。当本批次跨越SQ末尾时，会分别复制到SQ尾部和SQ起始位置。
- `BatchCommit`成功后始终敲SQ doorbell，不提供普通`Commit`接口中的`commit`或`pipe`模板参数。
- 空批次和同一空批次的重复提交返回`-1`。
- 提交成功后，BatchHandle和UB缓冲区可以继续用于准备下一个批次；接口只重置本批次WQEBB计数，不清空UB缓冲区内容。
- 可以连续执行多次`BatchCommit`，最后统一调用一次批量`Drain`等待已提交任务完成。
- 多个批次在`Drain`前连续提交时，调用方必须保证累计未完成任务不会覆盖SQ中尚未消费的WQE。
- 调用方还需要保证自上次批量`Drain`以来累计生成但尚未消费的CQE不超过CQ容量；接口不在提交阶段自动轮询CQ。
- BatchHandle使用期间，调用方需要独占对应通道，不能在同一通道上混用普通`ChannelHandle`提交流程。

## 相关接口

- [MakeBatchHandle](./MakeBatchHandle.md)
- [Drain](./Drain.md)
