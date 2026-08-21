# WriteWithNotifyNbi

## Function Description

Writes data from local `src` to remote `dst` and carries a remote notification address and value. The interface provides an ordinary `ChannelHandle` overload and a BatchHandle overload. The ordinary overload submits the task to the channel, while the batch overload only prepares a write-with-notify WQE in the UB buffer of the BatchHandle for a later `BatchCommit`.

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
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
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
| `batchHandle` | Input/Output | Batch handle created by `MakeBatchHandle`. Its WQEBB count is updated after a WQE is prepared successfully; its local `cqHead` is also incremented when `cqe = 1`. |
| `dst` | Output | Absolute remote destination GM address. For the batch interface, the caller must ensure that its access range matches the `tokenId/tokenValue` cached in `batchHandle`. |
| `src` | Input | Absolute local source GM address. |
| `len` | Input | Write length in bytes. |
| `notifyAddr` | Input | Absolute remote notification GM address. For the batch interface, the caller must ensure that its access range matches the `tokenId/tokenValue` cached in `batchHandle`. |
| `notifyVal` | Input | Remote notification value. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the ordinary interface commits the task immediately. The batch interface does not provide this parameter. |
| `commitPipe` | Pipe used for commit by the ordinary interface. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for the request by the ordinary interface. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG` (strongly ordered + fence + CQE enabled). The batch interface requires `inlineEn = 0`, and `cqe` must be `0` or `1`. |
| `T` | Batch handle type, deduced from `batchHandle`. Currently supports `UbcCtpBatchHandle`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The ordinary task is submitted or the batch WQE is prepared successfully. |
| `-1` | The operation fails. For the batch interface, this includes insufficient space in the UB buffer. The ordinary `COMM_PROTOCOL_ROCE` path also returns failure. |

## Constraints

### Ordinary Interface

- The communication channel must be initialized, and `Init` must provide temporary workspace before this interface is called.
- The `ChannelHandle` must identify a `COMM_PROTOCOL_UBC_CTP` channel.
- The `COMM_PROTOCOL_ROCE` path does not support this interface and returns `-1`.
- An ordinary write-with-notify task occupies two WQEBBs in the URMA SQ.

### Batch Interface

- Currently supported only on the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Each batch write-with-notify task occupies two 64-byte WQEBBs. It is prepared only in UB and does not copy data to the GM SQ or ring the doorbell.
- `config.inlineEn` must be `0`, and `config.cqe` can be `0` or `1`. Different read, write, and write-with-notify tasks in the same batch may use different `cqe` settings.
- `dst` and `notifyAddr` are not checked against the remote registration range again, and the remote registration table is not queried. The notification context reuses the `tokenId/tokenValue` cached in the BatchHandle. The caller must ensure that both remote access ranges belong to the same registered memory from which `MakeBatchHandle` cached the token using `remoteAddr`.
- If buffer capacity validation fails, the interface returns `-1` and leaves the BatchHandle WQEBB count and `cqHead` unchanged.
- Call `BatchCommit` after preparation. The caller must exclusively own the channel while using the BatchHandle.

## Related Interfaces

- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
