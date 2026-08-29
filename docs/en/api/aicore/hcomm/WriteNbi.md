# WriteNbi

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Supports the RoCE protocol but not the URMA protocol
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Supports the RoCE protocol but not the URMA protocol
- Atlas inference series products Vector Core: Supports the RoCE protocol but not the URMA protocol
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Supports the RoCE protocol but not the URMA protocol
<!-- end id7 -->

## Function Description

Writes data from local `src` to remote `dst`. The interface provides an ordinary `ChannelHandle` overload and a BatchHandle overload: the ordinary overload directly submits a write task to the channel; the batch overload only prepares a write WQE in the UB buffer of the BatchHandle, which is submitted together later by `BatchCommit`.

## Function Prototype

Ordinary interface:

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

Batch interface:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
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

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The ordinary task is submitted successfully, or the batch WQE is prepared successfully. |
| `-1` | The operation fails. For the batch interface, this includes insufficient remaining space in the UB buffer. |

## Constraints

### Ordinary Interface

- The communication channel must be initialized before invocation, and temporary workspace must be provided through `Init`.
- On the `COMM_PROTOCOL_UBC_CTP` path, `dst` must fall within the range of the remote buffer registered for the channel, and `src` is the local source address.
- If the `commit` template parameter is set to `false`, the number of consecutive calls must not exceed `sqDepth`. Pending tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise subsequent `PostSend` fails due to SQ overflow.
- In a batched-submission scenario (multiple delayed commits + one final commit), only the last commit should produce a CQE (that is, `config.cqe` is set to `0` for intermediate tasks and `1` for the last one).

### Batch Interface

- Currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Each batch write task occupies one 64-byte WQEBB. It is only prepared in UB, is not copied to the GM SQ, and does not ring the doorbell.
- `config.inlineEn` must be `0`, and `config.cqe` supports `0` or `1`. Different read, write, and write-with-notify tasks in the same batch can use different `cqe` settings.
- For a batch handle, `[dst, dst + len)` must belong to the remote registered memory selected by the handle.
- When buffer capacity validation fails, `-1` is returned, and the WQEBB count and `cqHead` in the BatchHandle remain unchanged.
- `BatchCommit` must be called after preparation. In multi-channel mode, prepare WQEs through the inner BatchHandle reference returned by `GetHandleRef` and submit through the outer `UbcCtpMultiBatchHandle`. While in use, the corresponding single channel or shared Jetty must be exclusively owned.

## Example

The calling example of WriteNbi is as follows:

```cpp
// Ordinary interface
hcomm.WriteNbi(channel, writeDst, writeSrc, DATA_SIZE);
hcomm.Drain(channel);

// Batch interface
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen);
hcomm.WriteNbi(batchHandle, dst, src, len);
hcomm.BatchCommit(batchHandle);
hcomm.Drain(batchHandle);
```
