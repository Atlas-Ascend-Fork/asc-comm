# AtomicCAS

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:39:14.211Z pushedAt=2026-11-09T06:47:54.971Z nl=2 -->

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

Adds a compare-and-swap atomic operation task that compares the value at the remote address `dst` with `compareVal` and, if they are equal, replaces the remote value with `swapVal` and writes the old value before replacement to the local `fetchAddr`. The API provides a normal `channel` overload, a batch `batchHandle` overload, and a batch group cooperative overload: the normal overload directly adds an atomic operation task to the channel; the batch overload adds the atomic operation task to the current batch, which is later committed uniformly by [BatchCommit](./SIMT_BatchCommit.md); the group cooperative overload has multiple SIMT threads in the group cooperatively add batch tasks.

## Function Prototype

Common API:

```cpp
template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);
```

Batch API:

```cpp
template <typename T, auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);
```

Batch group cooperative API:

```cpp
template <typename T, auto const& config = URMA_DEFAULT_CFG, typename Group>
__simt_callee__ inline int32_t AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal,
    const Group& group);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Atomic operation data type. Only `int32_t`, `uint32_t`, `int64_t`, and `uint64_t` are supported. |
| `commit` | Whether the common API commits immediately when submitting the task. `false` indicates deferred commit, which is later triggered uniformly by a call with `commit` set to `true`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required). |
| `Group` | SIMT cooperative group type, used by the batch group cooperative overload. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the common API. |
| `batchHandle` | Input/Output | Batch handle created by [MakeBatchHandle](./SIMT_MakeBatchHandle.md). |
| `dst` | Output | Remote GM (Global Memory) address of the atomic operation target. |
| `fetchAddr` | Output | Local GM address used to save the old value before the CAS is executed on the remote address. |
| `compareVal` | Input | Value to compare. |
| `swapVal` | Input | New value written to the remote target address when the comparison succeeds. |
| `group` | Input | SIMT cooperative group, used by the batch group cooperative overload. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | The normal task is submitted successfully, or the atomic operation task is successfully added to the current batch. |
| `-1` | The operation fails. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.

**Common API**

- The communication channel resources must be initialized before the call.
- `dst` must fall within the remote registered memory range registered for the channel. `fetchAddr` stores the old value and has a length of `sizeof(T)`.
- A single common atomic operation task occupies 2 task slots (128 bytes) regardless of the submission method. When the `commit` template parameter is set to `false`, the sum of the slots occupied by accumulated deferred-commit tasks and the slots occupied by the `commit`-as-`true` task that publishes the batch must not exceed `sqDepth`. The accumulated tasks must be submitted by a `commit`-as-`true` task before the SQ is exhausted; otherwise, subsequent task submissions fail due to SQ overflow.
- In the deferred-commit scenario (multiple deferred commits + one final commit), only the final commit should generate a completion record (that is, `config.cqe` of intermediate tasks is set to **0**, and that of the final task is set to **1**).

**Batch API**

- After adding tasks, call [BatchCommit](./SIMT_BatchCommit.md) to submit the current batch.
- The `dst` accessed by batch tasks must fall within the remote registered memory of the channel to which the batch handle belongs. This constraint is guaranteed by the caller.
- Requests appended within the same batch must use the same WQE width (that is, occupy the same number of task slots; for example, write-notify/atomic tasks occupy 2 task slots, and common write/read tasks occupy 1). Mixing widths is not supported, and the task addition returns `-1`.
- The batch overload does not provide a `commit` parameter and always adds tasks with deferred semantics. Each batch atomic operation task occupies 2 task slots (128 bytes).

## Call Example

The following shows how to call the AtomicCAS API. To focus on the call flow, the examples omit return value checks and error handling. For complete examples of the common API, see [04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md). For complete examples of the batch API, see [06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md).

Common API:

```cpp
// Comparison value.
constexpr uint64_t COMPARE_VAL = 0U;
// New value to write to the remote target address when the comparison succeeds.
constexpr uint64_t SWAP_VAL = 1U;

__simt_callee__ void SimtAtomicCasOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* remoteValue, __gm__ uint8_t* fetchBuffer)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // When the remote remoteValue equals COMPARE_VAL, replace it with SWAP_VAL, and write the old value before replacement to the local fetchBuffer.
    hcomm.AtomicCAS<uint64_t>(channel, remoteValue, fetchBuffer, COMPARE_VAL, SWAP_VAL);

    // Block until the atomic operation task completes.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Comparison value.
constexpr uint64_t COMPARE_VAL = 0U;
// New value to write to the remote target address when the comparison succeeds.
constexpr uint64_t SWAP_VAL = 1U;
// Batch workspace size: no less than 256 bytes, and it does not grow with the number of batch tasks.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchAtomicCasOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* remoteValue, __gm__ uint8_t* fetchBuffer)
{
    // Declare the batch UB workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Create a batch handle and bind the batch workspace.
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // Add the current batch of tasks and commit them.
    hcomm.AtomicCAS<uint64_t>(batchHandle, remoteValue, fetchBuffer, COMPARE_VAL, SWAP_VAL);
    hcomm.BatchCommit(batchHandle);

    // Reuse the batch handle to add and commit the next batch of tasks.
    hcomm.AtomicCAS<uint64_t>(batchHandle, remoteValue, fetchBuffer, COMPARE_VAL, SWAP_VAL);
    hcomm.BatchCommit(batchHandle);

    // Wait for all completion records generated by the accumulated committed tasks.
    hcomm.Drain(batchHandle);
}
```

Batch group cooperative API:

```cpp
#include "simt_api/cooperative_groups.h"

// Comparison value.
constexpr uint64_t COMPARE_VAL = 0U;
// New value written to the remote target address when the comparison succeeds.
constexpr uint64_t SWAP_VAL = 1U;
// Batch workspace size: no less than 256 bytes, and it does not grow with the number of threads in the group.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupBatchAtomicCasOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* remoteValue, __gm__ uint8_t* fetchBuffer)
{
    // Declare the batch UB workspace. All threads in the group pass the same workspace; the start address is 16-byte aligned.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // All threads in the group create the batch handle with the same arguments.
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);

    // Each thread in the group adds an atomic operation task to the current batch.
    hcomm.AtomicCAS<uint64_t>(batchHandle, remoteValue, fetchBuffer, COMPARE_VAL, SWAP_VAL, group);

    // All threads in the group cooperatively submit all tasks in the current batch.
    hcomm.BatchCommit(batchHandle, group);

    // After all threads in the group are synchronized, a single thread waits for all completion records generated by the cumulatively submitted tasks.
    group.sync();
    if (group.thread_rank() == 0) {
        hcomm.Drain(batchHandle);
    }
}
```
