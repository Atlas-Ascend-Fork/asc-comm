# Drain

## Function Description

Blocks until communication tasks submitted through an ordinary channel or a BatchHandle complete execution.

The ordinary overload uses CQE scratch space provided through `Init`. The batch overload does not depend on `Init`; it reuses the first 64-byte WQEBB in the WQE buffer of the BatchHandle as CQE scratch space.

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
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t Drain(T& batchHandle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the ordinary interface. |
| `batchHandle` | Input/Output | Batch handle created by `MakeBatchHandle`. After a successful wait, the cached CQ tail and the CQ tail in the channel entity are updated. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for drain operations. Default: `PIPE_MTE3`. |
| `T` | Batch handle type, deduced from `batchHandle`. Used only by the batch overload and currently supports `UbcCtpBatchHandle`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Waiting succeeds. |
| `-1` | An argument or BatchHandle state is invalid, or the protocol returns a generic failure. |
| Other non-zero value | The `COMM_PROTOCOL_UBC_CTP` path returns the underlying CQ polling error code directly. `0xFF` indicates a polling timeout, while other positive values combine the CQE status and substatus. |

## Constraints

### Ordinary Interface

- Call `Init` to provide temporary workspace before this interface.
- This overload waits for CQEs recorded by the ordinary `ChannelHandle` workflow.

### Batch Interface

- Currently supported only on the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950. It does not require `Init`.
- Submit prepared WQEs through `BatchCommit` before this interface. If the BatchHandle contains uncommitted WQEBBs, this interface returns `-1`.
- Batch `Drain` reuses the first 64 bytes of the WQE buffer as CQE scratch space. Do not access or modify the buffer concurrently during the call.
- Multiple `BatchCommit` calls can be followed by one batch `Drain`, which waits for all CQEs accumulated in the BatchHandle.
- If all submitted tasks use `cqe = 0`, there is no CQE to poll and the interface returns success directly, but that return value does not confirm hardware completion. If ordering and fence settings guarantee that the final request completes after preceding requests, only the final request needs `cqe = 1`, and batch `Drain` can then confirm completion of the group.
- The caller must exclusively own the channel while using the BatchHandle. Do not mix ordinary `Drain` on the same channel.
