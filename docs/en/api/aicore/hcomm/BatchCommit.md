# BatchCommit

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

Copies the WQEs prepared in a batch handle from the UB buffer to the channel GM SQ, updates queue counters, and rings the SQ doorbell so that the tasks in this batch start to execute.

This interface currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

## Function Prototype

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type deduced from the `batchHandle` argument. Currently supports `UbcCtpBatchHandle` and `UbcCtpMultiBatchHandle`. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. On success, all prepared WQEs in it are submitted and the WQEBB count of the current batch is cleared. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Submission succeeded. |
| `-1` | Submission failed, including an empty batch, an invalid WQEBB count, or a batch WQEBB count that violates the SQ capacity constraint. |

## Constraints

- At least one WQE must be prepared through batch `ReadNbi`, `WriteNbi`, or `WriteWithNotifyNbi` before this interface is called.
- In multi-channel mode, prepare WQEs through the inner BatchHandle reference returned by `GetHandleRef` and pass the outer multi-channel batch handle to this interface.
- Batch read, write, and write-with-notify tasks can be mixed and prepared in the same batch.
- The number of WQEBBs prepared in this batch must not exceed the UB buffer capacity of the BatchHandle and must be strictly smaller than `sqDepth`.
- SQ ring wrap is supported. If the batch crosses the end of the SQ, the interface copies it to the tail and the beginning of the SQ separately.
- A successful `BatchCommit` always rings the SQ doorbell. It does not provide the `commit` or `pipe` template parameters of the ordinary `Commit` interface.
- Committing an empty batch, including repeatedly committing the same empty batch, returns `-1`.
- After a successful commit, the BatchHandle and UB buffer can continue to be used to prepare the next batch. The interface resets only the WQEBB count of the current batch and does not clear the UB buffer contents.
- Multiple `BatchCommit` calls can be executed consecutively, followed by one batch `Drain` that waits for the submitted tasks to complete.
- When multiple batches are submitted consecutively before `Drain`, the caller must ensure that the accumulated outstanding tasks do not overwrite SQ WQEs that have not yet been consumed.
- The caller must also ensure that CQEs generated but not consumed since the previous batch `Drain` do not exceed the CQ capacity. This interface does not poll the CQ automatically during submission.
- While the BatchHandle is in use, the caller must exclusively own the corresponding single channel or shared Jetty and must not mix the ordinary `ChannelHandle` submission workflow.

## Example

The calling example of BatchCommit is as follows:

```cpp
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen);
hcomm.WriteNbi(batchHandle, dst1, src1, len1);
hcomm.WriteNbi(batchHandle, dst2, src2, len2);
hcomm.BatchCommit(batchHandle);    // Commit all prepared WQEs
hcomm.Drain(batchHandle);    // Wait for tasks to complete
```
