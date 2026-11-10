# BatchCommit

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:41:59.020Z pushedAt=2026-11-09T06:47:54.973Z nl=2 -->

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

Header file path: `"hcomm/hcomm_simt.h"`.

Submits all tasks in the current batch of the batch handle and publishes the SQ range prepared by the preceding batch APIs. The API provides a single-threaded batch overload and a group cooperative batch overload: the single-threaded overload is submitted directly by the thread that created the handle, and currently only one task request can be submitted per batch; the group cooperative overload is a cooperative call, in which every thread in the group must call it to jointly complete the batch submission.

## Function Prototype

Single-threaded batch API:

```cpp
__simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle);
```

Group cooperative batch API:

```cpp
template <typename Group>
__simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle, const Group& group);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `Group` | SIMT cooperative group type, provided by `simt_api/cooperative_groups.h`, used only by the group cooperative overload. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Batch handle created by [MakeBatchHandle](./SIMT_MakeBatchHandle.md). On success, the current batch can add tasks again. |
| `group` | Input | SIMT cooperative group, used by the group cooperative batch overload. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | Submission succeeded. |
| `-1` | Submission failed. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- Before calling, add at least one task through a batch API (such as [WriteNbi](./SIMT_WriteNbi.md) or [ReadNbi](./SIMT_ReadNbi.md)).
- The current single-threaded batch API supports submitting only one task request per batch. To submit multiple tasks, add and submit them one by one, or use the group cooperative batch overload.
- If the API that previously added a task returns `-1` (SQ space reservation failed), do not call this API for the batch.
- The batch path requires `config.cqe` of each task to be 1 (the default `URMA_DEFAULT_CFG`/`URMA_INLINE_CFG` already satisfies this). If `cqe` is 0 in a custom `config`, a compilation error occurs.
- The group cooperative overload is a cooperative call: every thread in the group must call this API, and each thread must have added exactly one task to the current batch.
- After a successful submission, you can reuse the **BatchHandle** and workspace to add and submit the next batch.
- While using the **BatchHandle**, you must exclusively use its associated channel resources and must not mix in the normal `ChannelHandle` submission flow.
- Completion waiting for batch tasks is done through the batch overload of [Drain](./SIMT_Drain.md).
- Requests appended within the same batch must use the same WQE width (that is, occupy the same number of task slots; for example, write-notify/atomic tasks occupy 2 task slots, and normal write/read tasks occupy 1). Mixing widths is not supported, and task addition returns `-1`.

## Call Example

The following shows an example of calling the **BatchCommit** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample, see [06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md).

Single-threaded batch API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Batch workspace size: no less than 256 bytes, and does not grow with the number of batch tasks.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchCommitOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // Declare the batch UB workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Create the batch handle and bind it to the batch workspace.
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // Submit the first batch of tasks.
    hcomm.WriteNbi(batchHandle, remoteBase, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // Reuse the handle after the batch is submitted successfully, and submit the next batch of tasks.
    hcomm.WriteNbi(batchHandle, remoteBase + BYTES, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // Wait for all completion records generated by the accumulated submitted tasks.
    hcomm.Drain(batchHandle);
}
```

Group cooperative batch API:

```cpp
#include "simt_api/cooperative_groups.h"

// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Batch workspace size: at least 256 bytes, and it does not grow with the number of threads in the group.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupBatchCommitOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // Declare the batch UB workspace. All threads in the group pass the same workspace, and the start address is 16-byte aligned.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // All threads in the group create the batch handle with the same arguments.
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);

    // Each thread in the group adds a write task to the current batch, offsetting its remote address by its thread rank.
    hcomm.WriteNbi(batchHandle, remoteBase + group.thread_rank() * BYTES, localBuffer, BYTES, group);

    // Cooperatively submit all tasks in the current batch within the group.
    hcomm.BatchCommit(batchHandle, group);

    // After all threads in the group synchronize, a single thread waits for all completion records generated by the cumulatively submitted tasks.
    group.sync();
    if (group.thread_rank() == 0) {
        hcomm.Drain(batchHandle);
    }
}
```
