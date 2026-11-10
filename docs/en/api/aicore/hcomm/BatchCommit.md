# BatchCommit

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:25:28.756Z pushedAt=2026-11-09T06:47:54.962Z nl=2 -->

## Supported Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference products: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training products: Not supported
<!-- end id6 -->

## Function Description

Header file path: `"hcomm/hcomm.h"`.

Submits all tasks in the current batch of **BatchHandle**.

## Function Prototype

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type, deduced from the `batchHandle` argument. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by [MakeBatchHandle](./MakeBatchHandle.md). On success, the current batch can accept new tasks. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | Submitted successfully. |
| `-1` | Submission failed. |

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- Before calling this API, add at least one task through batch [WriteNbi](./WriteNbi.md), [ReadNbi](./ReadNbi.md), or [WriteWithNotifyNbi](./WriteWithNotifyNbi.md) (only for the `COMM_PROTOCOL_UB_CTP` protocol).
- Under the `COMM_PROTOCOL_UB_CTP` protocol, read, write, and write-with-notify tasks can be mixed within a batch; the `COMM_PROTOCOL_ROCE` protocol supports only batch read and write tasks.
- The workspace used by the current batch must not exceed the `buffLen` specified when creating the **BatchHandle**, and the number of tasks must satisfy the channel capacity limits (the task submission capacity is the SQ depth `sqDepth`, and the completion record capacity is the CQ depth `cqDepth`).
- In multi-channel mode, add tasks through the handle reference returned by [GetHandleRef](./GetHandleRef.md), and call this API through the multi-channel batch handle returned by [MakeBatchHandle](./MakeBatchHandle.md).
- Multi-channel batch is supported only by the `COMM_PROTOCOL_UB_CTP` protocol; `COMM_PROTOCOL_ROCE` does not support multi-channel mode.
- After a successful submission, reuse the **BatchHandle** and workspace to add and submit another batch.
- You can submit multiple batches consecutively and then call batch [Drain](./Drain.md) once. The caller must ensure that the cumulative number of outstanding tasks and completion records does not exceed the corresponding capacity configured for the channel.
- While using a **BatchHandle**, exclusively own its associated channel resources. Do not mix the ordinary `ChannelHandle` submission workflow.

## Call Example

The following shows an example of calling the **BatchCommit** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample, see [02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md).

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Number of write tasks added in a single batch.
constexpr uint32_t TASK_NUM = 4U;
// Batch workspace size: divided into 64-byte task slots, and must accommodate the slots of the entire batch (the total number of slots must be less than the channel's task submission capacity).
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Declare the batch UB workspace. The batch API does not depend on Init; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // Create the Hcomm object.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // Construct the batch workspace LocalTensor from the UB workspace start address.
    AscendC::LocalTensor<uint8_t> batchBuffer(
        AscendC::TPosition::VECOUT,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

    // Create a single-channel batch handle and bind it to the batch workspace. remoteBuffer selects the remote registered memory.
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // Add TASK_NUM write tasks to the current batch in sequence, writing to different remote offsets. The tasks are first accumulated in the batch workspace and not yet submitted.
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.WriteNbi(batchHandle, remoteBuffer + i * BYTES, localBuffer, BYTES);
    }

    // Submit all tasks accumulated in the current batch at once.
    hcomm.BatchCommit(batchHandle);

    // Block and wait for all completion records generated by the entire batch of tasks.
    hcomm.Drain(batchHandle);
}
```
