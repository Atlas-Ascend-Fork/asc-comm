# WriteWithNotifyNbi

## Function Description

Writes data from local `src` to remote `dst` and carries a remote notification address and value. The interface provides an ordinary `ChannelHandle` overload and a BatchHandle overload. The ordinary overload submits the task to the channel, while the batch overload adds a write-with-notify task to the current batch for a later `BatchCommit`.

Both overloads support only the `COMM_PROTOCOL_UBC_CTP` path. The batch overload currently supports only Ascend 950.

## Function Prototype

Ordinary interface:

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteWithNotifyNbi(
    AscendC::ChannelHandle channel,
    GM_ADDR dst,
    GM_ADDR src,
    uint64_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

Batch interface:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteWithNotifyNbi(
    T& batchHandle,
    GM_ADDR dst,
    GM_ADDR src,
    uint32_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the ordinary interface. |
| `batchHandle` | Input/Output | Single-channel batch handle created by `MakeBatchHandle`, or a BatchHandle reference returned by `GetHandleRef`. |
| `dst` | Output | Absolute remote destination GM address. |
| `src` | Input | Absolute local source GM address. |
| `len` | Input | Write length in bytes. |
| `notifyAddr` | Input | Absolute remote notification GM address. For the batch interface, this address and `dst` must belong to the same remote registered memory. |
| `notifyVal` | Input | Remote notification value. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the ordinary interface commits the task immediately. The batch interface does not provide this parameter. |
| `commitPipe` | Pipe used for commit by the ordinary interface. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for the request by the ordinary interface. Default: `PIPE_MTE3`. |
| `config` | URMA task configuration. Default: `URMA_DEFAULT_CFG`. The batch interface requires `inlineEn = 0`, and `cqe` must be `0` or `1`. |
| `T` | Batch handle type, deduced from `batchHandle`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The ordinary task is submitted, or the write-with-notify task is added to the current batch successfully. |
| `-1` | The operation fails. For the batch interface, this includes insufficient space in the batch workspace. The ordinary `COMM_PROTOCOL_ROCE` path also returns failure. |

## Constraints

### Ordinary Interface

- The communication channel must be initialized, and `Init` must provide temporary workspace before this interface is called.
- The `ChannelHandle` must identify a `COMM_PROTOCOL_UBC_CTP` channel.
- The `COMM_PROTOCOL_ROCE` path does not support this interface and returns `-1`.
- An ordinary write-with-notify task occupies two task-submission slots.

### Batch Interface

- Currently supported only on the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Each batch write-with-notify task uses 128 bytes of batch workspace.
- The `dst` range and `notifyAddr` must belong to the same remote registered memory selected when creating or configuring the BatchHandle.
- The interface returns `-1` if the remaining batch workspace is insufficient.
- Call `BatchCommit` after adding tasks. In multi-channel mode, add tasks through the handle reference returned by `GetHandleRef` and submit them through the multi-channel batch handle returned by `MakeBatchHandle`. Exclusively own the associated channel resources while using the BatchHandle.

## Related APIs

- [MakeBatchHandle](./MakeBatchHandle.md)
- [GetHandleRef](./GetHandleRef.md)
- [BatchCommit](./BatchCommit.md)
