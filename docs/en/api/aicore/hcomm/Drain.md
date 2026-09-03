# Drain

## Function Description

Blocks until communication tasks submitted through an ordinary channel or a BatchHandle complete execution.

The ordinary overload uses temporary workspace provided through `Init`. The batch overload uses the workspace bound by `MakeBatchHandle` and does not depend on `Init`.

## Function Prototype

Ordinary interface:

```cpp
template <pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t Drain(AscendC::ChannelHandle channel);
```

Batch interface:

```cpp
template <
    pipe_t pipe = PIPE_MTE3,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t Drain(T& batchHandle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the ordinary interface. |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. The call waits for communication tasks submitted through this handle. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for drain operations. Default: `PIPE_MTE3`. |
| `T` | Batch handle type deduced from `batchHandle`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Waiting succeeds. |
| `-1` | An argument or BatchHandle state is invalid, or the protocol returns a generic failure. |
| Other non-zero value | Waiting fails. |

## Constraints

### Ordinary Interface

- Call `Init` to provide temporary workspace before this interface.
- This overload waits for CQEs recorded by the ordinary `ChannelHandle` workflow.

### Batch Interface

- Currently supported only on the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950. It does not require `Init`.
- In multi-channel mode, pass the multi-channel batch handle returned by `MakeBatchHandle` to batch `Drain`.
- Submit all tasks in the current batch through `BatchCommit` before calling this API. It returns `-1` if the BatchHandle contains unsubmitted tasks.
- Multiple `BatchCommit` calls can be followed by one batch `Drain`, which waits for all tasks submitted through that BatchHandle to complete.
- If all submitted tasks use `cqe = 0`, this API returns directly and does not confirm that those tasks have completed. To confirm completion of the batch through this API, set `cqe = 1` for a task that represents completion of the entire batch. The default `URMA_DEFAULT_CFG` enables this option.
- Exclusively own the channel resources associated with the BatchHandle while using it. Do not mix ordinary `Drain` calls.

## Related APIs

- [GetHandleRef](./GetHandleRef.md)
- [BatchCommit](./BatchCommit.md)
