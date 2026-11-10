# ReadNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:48:10.785Z pushedAt=2026-11-09T06:47:54.975Z nl=2 -->

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

Reads data from the remote `src` to the local `dst`. The API provides a normal `channel` overload, a batch `batchHandle` overload, and a batch group cooperative overload: the normal overload directly adds a read task to the channel; the batch overload adds the read task to the current batch, which is later committed together by [BatchCommit](./SIMT_BatchCommit.md); the group cooperative overload has multiple SIMT threads in the group collaborate to add batch tasks.

## Function Prototype

Common API:

```cpp
template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
```

Batch API:

```cpp
template <auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len);
```

Batch group cooperative API:

```cpp
template <auto const& config = URMA_DEFAULT_CFG, typename Group>
__simt_callee__ inline int32_t ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the common API commits immediately when submitting a task. `false` indicates delayed submission, which is later triggered together by a call with `commit` set to `true`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required). |
| `Group` | SIMT cooperative group type, such as a `cooperative_groups` thread partition, used by the batch group cooperative overload. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the common API. |
| `batchHandle` | Input/Output | Batch handle created by [MakeBatchHandle](./SIMT_MakeBatchHandle.md). |
| `dst` | Output | Local destination GM (Global Memory) address. |
| `src` | Input | Remote source GM address. |
| `len` | Input | Read length in bytes. It must not exceed 256 MB (256 * 1024 * 1024 bytes). |
| `group` | Input | SIMT cooperative group, used by the batch group cooperative overload. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | The common task is submitted successfully, or the read task is successfully added to the current batch. |
| `-1` | The operation fails. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- `src` must fall within the remote registered memory range registered for the channel, and `dst` is the local destination address.
- `len` is the transfer length in bytes, which must not exceed 256 MB (256 * 1024 * 1024 bytes).

**Common API**

- The communication channel resources must be initialized before the call.
- For immediate submission (`commit` is `true`), `len` must be at least 2 bytes.
- When the `commit` template parameter is set to `false`, the task is submitted with delayed submission and must later be triggered for submission by a call with `commit` set to `true`.
- The task slot usage of a single normal read task depends on the submission mode: immediate submission (`commit` is `true`) occupies 2 task slots (128 bytes), while delayed submission (`commit` is `false`) occupies 1 task slot (64 bytes). In delayed submission, the sum of the accumulated task slots and the slots occupied by the `commit`-`true` task that publishes the batch must not exceed `sqDepth`. The accumulated tasks must be submitted by a `commit`-`true` task before the SQ is exhausted; otherwise, subsequent task submissions will fail due to SQ overflow.
- In the delayed submission scenario (multiple delayed commits followed by a final commit), only the final commit should generate a completion record (that is, `config.cqe` of the intermediate tasks is set to 0, and that of the final task is set to 1).

**Batch API**

- After adding, call [BatchCommit](./SIMT_BatchCommit.md) to submit the current batch.
- The `src` address range accessed by batch tasks must fall within the remote registered memory of the channel to which the batch handle belongs. This constraint is guaranteed by the caller.
- Requests appended within the same batch must use the same WQE width (that is, occupy the same number of task slots; for example, write-notify/atomic tasks occupy 2 task slots, and normal write/read tasks occupy 1). Mixing widths is not supported, and task addition returns `-1`.
- The batch overload does not provide a `commit` parameter and always adds tasks with delayed semantics. Each batch read task occupies 1 task slot (64 bytes).

## Call Example

The following shows an example of calling the **ReadNbi** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample of the common API, see [04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md). For a complete sample of the batch API, see [06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md).

Common API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;

__simt_callee__ void SimtReadOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBuffer)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Read data from the remote remoteBuffer to the local localBuffer.
    hcomm.ReadNbi(channel, localBuffer, remoteBuffer, BYTES);

    // Block and wait for the read task to complete.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Batch workspace size: no less than 256 bytes, and does not grow with the number of batch tasks.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchReadOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // Declare the batch UB workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Create a batch handle and bind it to the batch workspace.
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // Add the current batch of tasks and submit them.
    hcomm.ReadNbi(batchHandle, localBuffer, remoteBase, BYTES);
    hcomm.BatchCommit(batchHandle);

    // Reuse the batch handle to add and submit the next batch of tasks.
    hcomm.ReadNbi(batchHandle, localBuffer + BYTES, remoteBase + BYTES, BYTES);
    hcomm.BatchCommit(batchHandle);

    // Wait for all completion records generated by the cumulatively submitted tasks.
    hcomm.Drain(batchHandle);
}
```

Batch group cooperative API:

```cpp
#include "simt_api/cooperative_groups.h"

// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Batch workspace size: no less than 256 bytes, and does not grow with the number of threads in the group.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupBatchReadOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // Declare the batch UB workspace. All threads in the group pass the same workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // All threads in the group create the batch handle with the same arguments.
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);

    // Each thread in the group adds a read task to the current batch, offsetting its own read/write address by thread rank.
    const uint32_t laneOffset = group.thread_rank() * BYTES;
    hcomm.ReadNbi(batchHandle, localBuffer + laneOffset, remoteBase + laneOffset, BYTES, group);

    // All threads in the group cooperatively submit all tasks in the current batch.
    hcomm.BatchCommit(batchHandle, group);

    // After all threads in the group synchronize, a single thread waits for all completion records generated by the cumulatively submitted tasks.
    group.sync();
    if (group.thread_rank() == 0) {
        hcomm.Drain(batchHandle);
    }
}
```
