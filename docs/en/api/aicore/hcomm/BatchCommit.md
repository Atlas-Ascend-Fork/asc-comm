# BatchCommit

## Function Description

Copies all WQEs prepared in a batch handle from the UB buffer to the channel GM SQ, updates queue counters, and rings the SQ doorbell so that the batch can execute.

This interface currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

## Function Prototype

```cpp
template <
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Batch handle created by `MakeBatchHandle`. After a successful commit, its local `cqHead` is synchronized to the channel and its prepared WQEBB count is reset to zero. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type, deduced from `batchHandle`. Currently supports `UbcCtpBatchHandle`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The batch is committed successfully. |
| `-1` | The commit fails because the batch is empty, its WQEBB count is invalid, or the count violates the SQ capacity constraint. |

## Constraints

- Prepare at least one WQE by calling batch `ReadNbi`, `WriteNbi`, or `WriteWithNotifyNbi` before this interface.
- Batch read, write, and write-with-notify tasks can be mixed in the same batch.
- The number of prepared WQEBBs must not exceed the UB buffer capacity of the BatchHandle and must be strictly smaller than `sqDepth`.
- SQ ring wrap is supported. If the batch crosses the end of the SQ, the interface copies it to the tail and the beginning of the SQ separately.
- A successful `BatchCommit` always rings the SQ doorbell. It does not provide the `commit` or `pipe` template parameters of the ordinary `Commit` workflow.
- Committing an empty batch, including repeating a commit without preparing another batch, returns `-1`.
- After a successful commit, the BatchHandle and UB buffer can be reused for another batch. The interface resets the prepared WQEBB count but does not clear the UB buffer contents.
- Multiple `BatchCommit` calls can be followed by one batch `Drain` that waits for all submitted tasks.
- When multiple batches are submitted before `Drain`, the caller must ensure that accumulated outstanding tasks do not overwrite SQ WQEs that have not yet been consumed.
- The caller must also ensure that CQEs generated but not consumed since the previous batch `Drain` do not exceed the CQ capacity. This interface does not poll the CQ automatically during submission.
- The caller must exclusively own the channel while using the BatchHandle. Do not mix the ordinary `ChannelHandle` submission workflow on the same channel.

## Related Interfaces

- [MakeBatchHandle](./MakeBatchHandle.md)
- [Drain](./Drain.md)
