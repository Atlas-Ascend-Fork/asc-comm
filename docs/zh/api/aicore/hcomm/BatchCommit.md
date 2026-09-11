# BatchCommit

## 功能说明

提交BatchHandle当前批次中的全部任务。当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

## 函数原型

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄。成功后，当前批次可以重新添加任务。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败，例如当前批次为空、状态无效或任务数量超过允许容量。 |

## 约束说明

- 调用前需要通过批量`ReadNbi`、`WriteNbi`或`WriteWithNotifyNbi`至少添加一个任务。
- 一个批次内可以混合添加读、写和写通知任务。
- 当前批次使用的工作区不能超过创建BatchHandle时指定的`buffLen`，并且任务数量必须满足通道容量限制。
- 多通道模式下，通过`GetHandleRef`返回的句柄引用添加任务，通过`MakeBatchHandle`返回的多通道批量句柄调用本接口。
- 提交成功后，可以复用BatchHandle和工作区添加并提交下一个批次。
- 可以连续提交多个批次，再统一调用一次批量`Drain`。调用方必须保证累计未完成任务数量和完成记录数量不超过通道配置的相应容量。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通`ChannelHandle`提交流程。

## 相关接口

- [MakeBatchHandle](./MakeBatchHandle.md)
- [GetHandleRef](./GetHandleRef.md)
- [Drain](./Drain.md)
