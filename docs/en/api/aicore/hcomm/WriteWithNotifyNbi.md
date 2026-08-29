# WriteWithNotifyNbi

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol but not the RoCE protocol
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Not supported
<!-- end id7 -->

## Function Description

Writes data from local `src` to remote `dst` and carries a remote notification address and notification value. The interface provides an ordinary `ChannelHandle` overload and a BatchHandle overload: the ordinary overload directly submits the task to the channel; the batch overload only prepares a write-with-notify WQE in the UB buffer of the BatchHandle, which is submitted together later by `BatchCommit`.

Both the ordinary and batch overloads support only the `COMM_PROTOCOL_UBC_CTP` path. The batch overload currently supports only Ascend 950.

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

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the ordinary interface commits the task immediately when the task is submitted. The batch interface does not provide this parameter. |
| `commitPipe` | Pipe used for commit by the ordinary interface. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for the request by the ordinary interface. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG` (execution order: relax order + in-order CQE reporting + fence enabled + CQE reporting required). The batch interface requires `inlineEn = 0`, and `cqe` must be `0` or `1`. |
| `T` | Batch handle type deduced from the `batchHandle` argument. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the ordinary interface. |
| `batchHandle` | Input/Output | Single-channel batch handle created by `MakeBatchHandle`, or the BatchHandle reference returned by `GetHandleRef`. |
| `dst` | Output | Absolute remote destination GM address. |
| `src` | Input | Absolute local source GM address. |
| `len` | Input | Write length in bytes. |
| `notifyAddr` | Input | Absolute remote notification GM address. The batch interface reuses the token used by the data write operation, so the caller must ensure that the notification address belongs to the same registered memory. |
| `notifyVal` | Input | Remote notification value. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The ordinary task is submitted successfully, or the batch WQE is prepared successfully. |
| `-1` | The operation fails. For the batch interface, this includes insufficient remaining space in the UB buffer. The ordinary `COMM_PROTOCOL_ROCE` path also returns failure. |

## Constraints

### Ordinary Interface

- The communication channel must be initialized before invocation, and temporary workspace must be provided through `Init`.
- The passed `ChannelHandle` must correspond to a `COMM_PROTOCOL_UBC_CTP` channel.
- The `COMM_PROTOCOL_ROCE` path does not support this interface, and the call returns `-1`.
- A single ordinary write-with-notify task occupies 2 WQEBBs in the URMA SQ.
- If the `commit` template parameter is set to `false`, the number of consecutive calls must not exceed `sqDepth`. Pending tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise subsequent `PostSend` fails due to SQ overflow.
- In a batched-submission scenario (multiple delayed commits + one final commit), only the last commit should produce a CQE (that is, `config.cqe` is set to `0` for intermediate tasks and `1` for the last one).

### Batch Interface

- Currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Each batch write-with-notify task occupies 2 64-byte WQEBBs. They are only prepared in UB, are not copied to the GM SQ, and do not ring the doorbell.
- `config.inlineEn` must be `0`, and `config.cqe` supports `0` or `1`. Different read, write, and write-with-notify tasks in the same batch can use different `cqe` settings.
- For a batch handle, the data address and the notification address must belong to the remote registered memory selected by the handle.
- In both modes, `notifyAddr` reuses the token of the data write operation and is not queried or validated separately. The caller must ensure that the `dst` access range and `notifyAddr` belong to the same registered memory.
- When buffer capacity validation fails, `-1` is returned, and the WQEBB count and `cqHead` in the BatchHandle remain unchanged.
- `BatchCommit` must be called after preparation. In multi-channel mode, prepare WQEs through the inner BatchHandle reference returned by `GetHandleRef` and submit through the outer `UbcCtpMultiBatchHandle`. While in use, the corresponding single channel or shared Jetty must be exclusively owned.

## Example

The calling example of WriteWithNotifyNbi is as follows:

```cpp
// Ordinary interface
hcomm.WriteWithNotifyNbi(channel, dst, src, len, notifyAddr, notifyVal);
hcomm.Drain(channel);

// Batch interface
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen);
hcomm.WriteWithNotifyNbi(batchHandle, dst, src, len, notifyAddr, notifyVal);
hcomm.BatchCommit(batchHandle);
hcomm.Drain(batchHandle);
```
