# WriteWithNotifyNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:52:25.503Z pushedAt=2026-11-09T06:47:54.977Z nl=2 -->

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

Writes data from the local `src` to the remote `dst`, carrying the remote notification address and notification value. The API provides a common `channel` overload, a batch `batchHandle` overload, and a batch group cooperative overload: the common overload directly adds a write-notify task to the channel; the batch overload adds the write-notify task to the current batch, which is later committed uniformly by [BatchCommit](./SIMT_BatchCommit.md); the group cooperative overload has multiple SIMT threads in the group cooperatively add batch tasks.

## Function Prototype

Common API:

```cpp
template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal);
```

Batch API:

```cpp
template <auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal);
```

Batch group cooperative API:

```cpp
template <auto const& config = URMA_DEFAULT_CFG, typename Group>
__simt_callee__ inline int32_t WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal, const Group& group);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the common API commits immediately when submitting the task. `false` indicates deferred commit, which is later triggered uniformly by a call with `commit` set to `true`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required). |
| `Group` | SIMT cooperative group type, used by the batch group cooperative overload. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the common API. |
| `batchHandle` | Input/Output | Batch handle created by [MakeBatchHandle](./SIMT_MakeBatchHandle.md). |
| `dst` | Output | Remote destination GM (Global Memory) address. |
| `src` | Input | Local source GM address. |
| `len` | Input | Write length in bytes, which must not exceed 256MB (256 * 1024 * 1024 bytes). |
| `notifyAddr` | Input | Remote notification GM address. |
| `notifyVal` | Input | Remote notification value. |
| `group` | Input | SIMT cooperative group, used by the batch group cooperative overload. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | The common task is submitted successfully, or the write-notify task is successfully added to the current batch. |
| `-1` | The operation fails. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- The `dst` access range and `notifyAddr` must fall within the remote registered memory range of the channel.
- `len` is the transfer length in bytes and must not exceed 256MB (256 * 1024 * 1024 bytes).

**Common API**

- The communication channel resources must be initialized before the call.
- A single common write-notify task occupies 2 task slots (128 bytes) regardless of the submission method. When the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred commit tasks plus the slots occupied by the `commit`-`true` task that publishes the batch must not exceed `sqDepth`. The accumulated tasks must be committed by a `commit`-`true` task before the SQ is exhausted; otherwise, subsequent task submissions fail due to SQ overflow.
- In the deferred commit scenario (multiple deferred commits + a final commit), only the final commit should generate a completion record (that is, `config.cqe` of intermediate tasks is set to **0**, and that of the final task is set to **1**).

**Batch API**

- After adding is complete, call [BatchCommit](./SIMT_BatchCommit.md) to commit the current batch.
- The `dst` address range and `notifyAddr` accessed by batch tasks must fall within the remote registered memory of the channel to which the batch handle belongs. This constraint is guaranteed by the caller.
- Requests appended within the same batch must use the same WQE width (that is, occupy the same number of task slots; for example, write-notify/atomic tasks occupy 2 task slots, and common write/read tasks occupy 1). Mixing widths is not supported, and task addition returns `-1`.
- The batch overload does not provide the `commit` parameter and always adds tasks with deferred semantics. Each batch write-notify task occupies 2 task slots (128 bytes).

## Call Example

The following shows an example of calling the **WriteWithNotifyNbi** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample of the common API, see [04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md). For a complete sample of the batch API, see [06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md).

Common API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Notification value written to the remote notification address after the write task completes.
constexpr uint64_t NOTIFY_VAL = 1U;

__simt_callee__ void SimtWriteNotifyOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBuffer,
    __gm__ uint8_t* notifyAddr)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Write local localBuffer data to remote remoteBuffer, and write the notification value to remote notifyAddr after the task completes.
    hcomm.WriteWithNotifyNbi(channel, remoteBuffer, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);

    // Block until the write-notify task completes.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Notification value written to the remote notification address after the write task completes.
constexpr uint64_t NOTIFY_VAL = 1U;
// Batch workspace size: no less than 256 bytes, and it does not grow with the number of batch tasks.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchWriteNotifyOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase,
    __gm__ uint8_t* notifyAddr)
{
    // Declare the batch UB workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Create a batch handle and bind it to the batch workspace.
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // Add the tasks of the current batch and commit them.
    hcomm.WriteWithNotifyNbi(batchHandle, remoteBase, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);
    hcomm.BatchCommit(batchHandle);

    // Reuse the batch handle to add and commit the next batch of tasks.
    hcomm.WriteWithNotifyNbi(batchHandle, remoteBase + BYTES, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);
    hcomm.BatchCommit(batchHandle);

    // Wait for all completion records generated by the cumulatively committed tasks.
    hcomm.Drain(batchHandle);
}
```

Batch group cooperative API:

```cpp
#include "simt_api/cooperative_groups.h"

// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Notification value written to the remote notification address after the write task completes.
constexpr uint64_t NOTIFY_VAL = 1U;
// Batch workspace size: no less than 256 bytes and does not grow with the number of threads in the group.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupBatchWriteNotifyOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase,
    __gm__ uint8_t* notifyAddr)
{
    // Declare the batch UB workspace. All threads in the group pass the same workspace; the start address is 16-byte aligned.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // All threads in the group create a batch handle with the same arguments.
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);

    // Each thread in the group adds a write-notify task to the current batch, offsetting its own remote address by thread rank.
    hcomm.WriteWithNotifyNbi(
        batchHandle, remoteBase + group.thread_rank() * BYTES, localBuffer, BYTES, notifyAddr, NOTIFY_VAL, group);

    // Cooperatively commit all tasks in the current batch within the group.
    hcomm.BatchCommit(batchHandle, group);

    // After all threads in the group synchronize, a single thread waits for all completion records generated by the cumulatively committed tasks.
    group.sync();
    if (group.thread_rank() == 0) {
        hcomm.Drain(batchHandle);
    }
}
```
