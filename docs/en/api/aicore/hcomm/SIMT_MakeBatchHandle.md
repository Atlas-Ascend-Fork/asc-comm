# MakeBatchHandle

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:47:33.986Z pushedAt=2026-11-09T06:47:54.975Z nl=2 -->

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

Creates a batch handle and binds the UB workspace required for batch operations. The returned batch handle is of type `BatchHandle<ChannelHandle>` (that is, `UbcCtpBatchHandle`), which exclusively uses one channel. The remote data ranges and notification addresses accessed by batch tasks added through this handle must fall within the remote registered memory of that channel. For the single-threaded batch API, a single thread creates the handle and then directly adds tasks. For the group cooperative batch API, threads within the group add tasks to the same handle together. Tasks are submitted through [BatchCommit](./SIMT_BatchCommit.md) and awaited through [Drain](./SIMT_Drain.md).

## Function Prototype

Single-threaded batch API:

```cpp
__simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen);
```

Group cooperative batch API:

```cpp
template <typename Group>
__simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, const Group& group);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `Group` | SIMT cooperative group type, provided by `simt_api/cooperative_groups.h` (for example, a thread block group constructed by `cooperative_groups::this_thread_block()`). Used only by the group cooperative batch overload. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle, exclusively used by the returned batch handle. |
| `buff` | Input | UB batch workspace provided by the caller, with the start address being a pointer of type `__ubuf__`. |
| `buffLen` | Input | Available length of the batch workspace, in bytes. At least 256 bytes are required. |
| `group` | Input | SIMT cooperative group. Used by the group cooperative batch overload. |

## Return Value

Returns the batch handle corresponding to `channel`. On creation failure, a zero-value handle is returned (`context` is `nullptr`). The caller must check this field before performing any batch task addition, [BatchCommit](./SIMT_BatchCommit.md), or [Drain](./SIMT_Drain.md) operation. Apart from validity checking, the batch handle should be used as an opaque handle. This API does not return a status code, and the caller must ensure that the parameters satisfy the constraints.

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- `channel` must be valid, and the corresponding communication resources must have been initialized.
- The batch workspace is provided by the caller. The start address of `buff` must be 16-byte aligned, and `buffLen` must not be less than 256 bytes.
- Each remote data range and notification address added through the returned handle must fall within the remote registered memory of the channel. This constraint is guaranteed by the caller.
- Each thread adds exactly one task per batch.
- The group cooperative batch API requires all threads in the group to pass the same `buff` and `buffLen`. During the handle lifetime, the group members, lane rank (the thread's index within the cooperative group), and size must remain fixed.
- Requests appended within the same batch must use the same WQE width (that is, occupy the same number of task slots; for example, write-notification/atomic tasks occupy 2 task slots, while common write/read tasks occupy 1). Mixing widths is not supported, and task addition returns `-1`.
- A channel can be exclusively used by only one valid batch handle at a time. While the handle is alive, you must not use the common `ChannelHandle` submission API of this class on that channel, nor use other batch handles concurrently.
- The lifecycle of **BatchHandle**, channel resources, and `buff` is the caller's responsibility. These resources must not be released or concurrently reused before the **BatchHandle** usage is complete.

## Call Example

The following shows an example of calling the **MakeBatchHandle** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample, see [06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md).

Single-threaded batch API:

```cpp
// Batch workspace size: no less than 256 bytes, and does not grow with the number of batch tasks.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtMakeBatchHandleOp(AscendC::ChannelHandle channel)
{
    // Declare the batch UB workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Create a batch handle and bind it to the batch workspace.
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);
}
```

Group cooperative batch API:

```cpp
#include "simt_api/cooperative_groups.h"

// Batch workspace size: no less than 256 bytes, and it does not grow with the number of threads in the group.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupMakeBatchHandleOp(AscendC::ChannelHandle channel)
{
    // Declare the batch UB workspace. All threads in the group pass the same workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Use the current thread block as the cooperative group. All threads in the group call with the same arguments to jointly create the batch handle.
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);
}
```
