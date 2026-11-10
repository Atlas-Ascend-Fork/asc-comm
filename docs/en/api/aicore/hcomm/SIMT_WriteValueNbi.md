# WriteValueNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:52:54.054Z pushedAt=2026-11-09T06:47:54.978Z nl=2 -->

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

Adds an immediate write task: carries `value` in the task and writes it to the remote `dst`, without depending on the local source buffer. The API provides a common `channel` overload, a batch `batchHandle` overload, and a batch group cooperative overload: the common overload directly adds an immediate write task to the channel; the batch overload adds the immediate write task to the current batch, which is later committed uniformly by [BatchCommit](./SIMT_BatchCommit.md); the group cooperative overload has multiple SIMT threads within a group cooperatively add batch tasks.

## Function Prototype

Common API:

```cpp
template <typename T, bool commit = true, auto const& config = URMA_INLINE_CFG>
__simt_callee__ inline int32_t WriteValueNbi(ChannelHandle channel, __gm__ void* dst, T value);
```

Batch API:

```cpp
template <typename T, auto const& config = URMA_INLINE_CFG>
__simt_callee__ inline int32_t WriteValueNbi(UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value);
```

Batch group cooperative API:

```cpp
template <typename T, auto const& config = URMA_INLINE_CFG, typename Group>
__simt_callee__ inline int32_t WriteValueNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value, const Group& group);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Type of the immediate `value`. `sizeof(T)` is the write width, which cannot exceed 16 bytes and must fit into the WQE inline data area. |
| `commit` | Whether the common API commits immediately when submitting the task. `false` indicates delayed submission, which is later triggered uniformly by a call with `commit` set to `true`. |
| `config` | WQE control configuration, defaulting to `URMA_INLINE_CFG` (relax order execution + CQE order-preserving reporting + fence enabled + CQE reporting required + inline data carried). Immediate write requires `inlineEn` to be `1`; otherwise, a compilation error is reported. |
| `Group` | SIMT cooperative group type, used by the batch group cooperative overload. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the common API. |
| `batchHandle` | Input/Output | Batch handle created by [MakeBatchHandle](./SIMT_MakeBatchHandle.md). |
| `dst` | Output | Remote destination GM (Global Memory) address. |
| `value` | Input | Immediate value to write to the remote side, carried inline in the task as `sizeof(T)` bytes. |
| `group` | Input | SIMT cooperative group, used by the batch group cooperative overload. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | The common task is submitted successfully, or the immediate write task is successfully added to the current batch. |
| `-1` | The operation fails. |

## Usage Constraints

- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- `dst` must fall within the remote registered memory range of the channel.

**Common API**

- The communication channel resources must be initialized before the call.
- `sizeof(T)` must fit into the inline payload area of the WQE.
- When the `commit` template parameter is set to `false`, the task is submitted with a delay and must be triggered for submission later by a call with `commit` set to `true`.
- The slot occupancy of a single common immediate write task depends on the submission mode: immediate submission (`commit` set to `true`) occupies 2 task slots (128 bytes), while delayed submission (`commit` set to `false`) occupies 1 task slot (64 bytes). In delayed submission, the sum of the accumulated task slots and the slots occupied by the `commit`-`true` task that publishes the batch must not exceed `sqDepth`. The accumulated tasks must be submitted by a `commit`-`true` task before the SQ is exhausted; otherwise, subsequent task submissions will fail due to SQ overflow.
- In the delayed submission scenario (multiple delayed commits followed by a final commit), only the final commit should generate a completion record (that is, `config.cqe` of the intermediate tasks is set to **0**, and that of the final task is set to **1**).

**Batch API**

- After adding is complete, call [BatchCommit](./SIMT_BatchCommit.md) to submit the current batch.
- The `dst` address accessed by batch tasks must fall within the remote registered memory of the channel to which the batch handle belongs. This constraint is guaranteed by the caller.
- Requests appended within the same batch must use the same WQE width (that is, occupy the same number of task slots; for example, write-notify/atomic tasks occupy 2 task slots, while common write/read tasks occupy 1). Mixing widths is not supported, and task addition returns `-1`.
- The batch overload does not provide the `commit` parameter and always adds tasks with delayed submission semantics. Each batch immediate write task occupies 1 task slot (64 bytes).

## Call Example

The following shows an example of calling the **WriteValueNbi** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample of the common API, see [04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md). For a complete sample of the batch API, see [06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md).

Common API:

```cpp
// Write the immediate value to the remote memory.
constexpr uint32_t VALUE = 0x5A5A5A5AU;

__simt_callee__ void SimtWriteValueOp(AscendC::ChannelHandle channel, __gm__ uint8_t* remoteBuffer)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Inline-write the immediate VALUE to the start address of remoteBuffer in the remote memory.
    hcomm.WriteValueNbi(channel, remoteBuffer, VALUE);

    // Block and wait for the write task to complete.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Write the immediate to the remote memory.
constexpr uint32_t VALUE = 0x5A5A5A5AU;
// Batch workspace size: no less than 256 bytes, and does not grow with the number of batch tasks.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchWriteValueOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* remoteBase)
{
    // Declare the batch UB workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Create the batch handle and bind the batch workspace.
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // Add the current batch of tasks and submit them.
    hcomm.WriteValueNbi(batchHandle, remoteBase, VALUE);
    hcomm.BatchCommit(batchHandle);

    // Reuse the batch handle to add and submit the next batch of tasks.
    hcomm.WriteValueNbi(batchHandle, remoteBase + sizeof(uint32_t), VALUE);
    hcomm.BatchCommit(batchHandle);

    // Wait for all completion records generated by the accumulated submitted tasks.
    hcomm.Drain(batchHandle);
}
```

Batch group cooperative API:

```cpp
#include "simt_api/cooperative_groups.h"

// Write the immediate to the remote memory.
constexpr uint32_t VALUE = 0x5A5A5A5AU;
// Batch workspace size: no less than 256 bytes, and does not grow with the number of threads in the group.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupBatchWriteValueOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* remoteBase)
{
    // Declare the batch UB workspace. All threads in the group pass the same workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // All threads in the group create a batch handle with the same arguments.
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);

    // Each thread in the group adds an immediate write task to the current batch, offsetting its remote address by thread rank.
    hcomm.WriteValueNbi(batchHandle, remoteBase + group.thread_rank() * sizeof(uint32_t), VALUE, group);

    // The threads in the group cooperatively submit all tasks in the current batch.
    hcomm.BatchCommit(batchHandle, group);

    // After all threads in the group synchronize, a single thread waits for all completion records generated by the cumulatively submitted tasks.
    group.sync();
    if (group.thread_rank() == 0) {
        hcomm.Drain(batchHandle);
    }
}
```
