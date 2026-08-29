# Drain

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

Blocks until the communication tasks submitted through the specified ordinary channel or BatchHandle complete execution.

The ordinary overload depends on the CQE temporary workspace provided by `Init`; the batch overload does not depend on `Init` and instead reuses the first 64-byte WQEBB in the WQE buffer of the BatchHandle as CQE temporary space.

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

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for drain. Default: `PIPE_MTE3`. |
| `T` | Batch handle type deduced from the `batchHandle` argument. Currently supports `UbcCtpBatchHandle` and `UbcCtpMultiBatchHandle`. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the ordinary interface. |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`, used to wait for the communication tasks submitted through this handle to complete. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Waiting succeeds. |
| `-1` | An argument or the BatchHandle state is invalid, or the corresponding protocol returns a generic failure. |
| Other non-`0` value | The `COMM_PROTOCOL_UBC_CTP` path returns the underlying CQ polling error code directly: `0xFF` indicates a polling timeout, and other positive values are composed of the CQE status and substatus. |

## Constraints

### Ordinary Interface

- Call `Init` to provide temporary workspace before this interface.
- This overload waits for the CQEs recorded by the ordinary `ChannelHandle` workflow.

### Batch Interface

- Currently supported only on the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950. Calling `Init` is not required.
- In multi-channel mode, pass the outer multi-channel batch handle to batch `Drain`.
- The prepared WQEs must be submitted through `BatchCommit` before this interface. The BatchHandle must not contain uncommitted WQEBBs; otherwise `-1` is returned.
- Batch `Drain` reuses the first 64 bytes of the WQE buffer as CQE temporary space. Do not concurrently access or modify the buffer during the call.
- Multiple `BatchCommit` calls can be executed before one batch `Drain` is called. The interface waits for all CQEs accumulated in the BatchHandle.
- When all submitted tasks are configured with `cqe = 0`, there is no CQE to poll and the interface returns success directly, but that return value cannot be used to confirm that the hardware has completed these requests. When the ordering and fence configurations guarantee that the last request completes after the preceding requests, only the last request needs to be configured with `cqe = 1`, and batch `Drain` can then confirm completion of the whole group of tasks.
- While the BatchHandle is in use, the caller must exclusively own the corresponding single channel or shared Jetty and must not mix ordinary `Drain`.

## Example

The calling example of Drain is as follows:

```cpp
// Ordinary interface
hcomm.WriteNbi(channel, dst, src, len);
hcomm.Drain(channel);    // Wait for ordinary channel tasks to complete

// Batch interface
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen);
hcomm.WriteNbi(batchHandle, dst, src, len);
hcomm.BatchCommit(batchHandle);
hcomm.Drain(batchHandle);    // Wait for batch tasks to complete
```
