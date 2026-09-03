# BatchCommit

## Function Description

Submits all tasks in the current batch of a BatchHandle. This API currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

## Function Prototype

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. After success, tasks can be added to a new batch. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type deduced from `batchHandle`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Submission succeeds. |
| `-1` | Submission fails, for example because the current batch is empty, its state is invalid, or its task count exceeds the allowed capacity. |

## Constraints

- Add at least one task through batch `ReadNbi`, `WriteNbi`, or `WriteWithNotifyNbi` before calling this API.
- Read, write, and write-with-notify tasks can be mixed in one batch.
- The workspace used by the current batch must not exceed the `buffLen` specified when creating the BatchHandle, and the number of tasks must satisfy the channel capacity limit.
- In multi-channel mode, add tasks through the handle reference returned by `GetHandleRef` and call this API with the multi-channel batch handle returned by `MakeBatchHandle`.
- After a successful submission, reuse the BatchHandle and workspace to add and submit another batch.
- Multiple batches can be submitted before one batch `Drain`. The caller must ensure that accumulated incomplete tasks and completion records do not exceed their corresponding configured channel capacities.
- While using a BatchHandle, exclusively own its associated channel resources. Do not mix the ordinary `ChannelHandle` submission workflow.

## Related APIs

- [MakeBatchHandle](./MakeBatchHandle.md)
- [GetHandleRef](./GetHandleRef.md)
- [Drain](./Drain.md)
