# WriteWithNotifyNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T10:00:53.513Z pushedAt=2026-11-09T06:47:54.982Z nl=2 -->

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

Writes data from the local `src` to the remote `dst`, carrying the remote notification address and notification value. The API provides a common `channel` overload and a batch `batchHandle` overload: the common overload directly adds a task to the channel; the batch overload adds the write-notify task to the current batch, which is later committed uniformly by [BatchCommit](./BatchCommit.md).

Both the common and batch overloads support only the `COMM_PROTOCOL_UB_CTP` protocol.

## Function Prototype

Common API:

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteWithNotifyNbi(
    AscendC::ChannelHandle channel,
    GM_ADDR dst,
    GM_ADDR src,
    uint64_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

Batch API:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteWithNotifyNbi(
    T& batchHandle,
    GM_ADDR dst,
    GM_ADDR src,
    uint32_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

Batch multiple SGE (Scatter-Gather Element) API:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteWithNotifyNbi(
    T& batchHandle,
    GM_ADDR dst,
    const BufDesc* srcDescs,
    uint32_t srcNum,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the common API commits immediately when submitting a task. The batch API does not provide this parameter. |
| `commitPipe` | Pipe used by the common API for commit, defaulting to `PIPE_S`. |
| `reqPipe` | Pipe used by the common API for requests, defaulting to `PIPE_MTE3`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required). |
| `T` | Batch handle type, deduced from the `batchHandle` argument. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the common API. |
| `batchHandle` | Input/Output | Single-channel batch handle created by [MakeBatchHandle](./MakeBatchHandle.md), or the **BatchHandle** reference returned by [GetHandleRef](./GetHandleRef.md). |
| `dst` | Output | Remote destination GM (Global Memory) absolute address. |
| `src` | Input | Local source GM absolute address. |
| `len` | Input | Write length in bytes, which must not exceed 256 MB (256 * 1024 * 1024 bytes). |
| `notifyAddr` | Input | Remote notification GM absolute address. In the batch API, this address must belong to the same remote registered memory as `dst`. |
| `notifyVal` | Input | Remote notification value. |
| `srcDescs` | Input | Local scatter-gather element descriptor array (batch multiple SGE API), where each element contains `addr` (segment start GM address) and `len` (segment length in bytes). |
| `srcNum` | Input | Number of memory segments (batch multiple SGE API). |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | The common task is submitted successfully, or the write-notify task is successfully added to the current batch. |
| `-1` | The operation fails. For the batch API, this includes insufficient remaining space in the batch workspace. |

## Usage Constraints

- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.

**Common API**

- Before the call, the communication channel must be initialized and a temporary workspace must be provided through `Init`.
- The passed `ChannelHandle` must correspond to a `COMM_PROTOCOL_UB_CTP` channel.
- A single `WriteWithNotifyNbi` task occupies 2 task slots (128 bytes). When the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred-commit tasks must not exceed `sqDepth`. The accumulated tasks must be submitted through `Commit` or automatic commit before the SQ is exhausted; otherwise, subsequent task submissions will fail due to SQ overflow.
- In the deferred-commit scenario (multiple deferred commits + one final commit), only the final commit should generate a completion record (that is, `config.cqe` is set to **0** for intermediate tasks and set to **1** for the final task).

**Batch API**

- The `dst` access range and `notifyAddr` must belong to the same remote registered memory specified when creating or selecting the **BatchHandle**.
- The batch multi-SGE API aggregates the discrete memory segments given by `srcDescs` in array order and writes them consecutively to the range starting at the remote `dst`, then writes the notification value to `notifyAddr` after the write completes. `srcNum` ranges from 1 to 11; a task fails to be added if the value is out of range. The validity of the address and length of each data segment is the caller's responsibility.
- After the addition is complete, call [BatchCommit](./BatchCommit.md). In multi-channel mode, add tasks through the handle reference returned by [GetHandleRef](./GetHandleRef.md), and commit them through the multi-channel batch handle returned by [MakeBatchHandle](./MakeBatchHandle.md). The associated channel resources must be exclusively occupied during use.

## Call Example

The following shows an example of calling the **WriteWithNotifyNbi** API. To focus on the call flow, the example omits return value checks and error handling.

Common API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Notification value written to the remote notification address after the write task completes.
constexpr uint64_t NOTIFY_VAL = 1U;
// Size of the UB temporary workspace to initialize: must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_with_notify_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer,
    GM_ADDR notifyAddr)
{
    // Prepare the UB temporary workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Write localBuffer data to remoteBuffer, and write the notification value to notifyAddr after the task completes.
    // A single common write-notify task occupies 2 task submission slots.
    hcomm.WriteWithNotifyNbi(
        channel, remoteBuffer, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);

    // Block and wait for the write-notify task to complete.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Notification value written to the remote notification address after the write task completes.
constexpr uint64_t NOTIFY_VAL = 1U;
// Number of write-notify tasks added in a single batch.
constexpr uint32_t TASK_NUM = 4U;
// Batch workspace size: A single batch write-notify task occupies 2 task slots (128 bytes), and the workspace must accommodate the entire batch of tasks (the total number of slots must be less than the channel's task submission capacity).
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 128U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_with_notify_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer,
    GM_ADDR notifyAddr)
{
    // Declare the batch UB workspace. The batch API does not depend on Init. alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // Create an Hcomm object.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // Construct the batch workspace LocalTensor from the UB workspace start address.
    AscendC::LocalTensor<uint8_t> batchBuffer(
        AscendC::TPosition::VECOUT,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

    // Create a single-channel batch handle and bind it to the batch workspace. remoteBuffer is used to select the remote registered memory.
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // Add TASK_NUM write-notify tasks to the current batch in sequence, each writing to a different remote offset, and write the notification value to notifyAddr after each task completes.
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.WriteWithNotifyNbi(
            batchHandle, remoteBuffer + i * BYTES, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);
    }

    // Submit all tasks accumulated in the current batch at once.
    hcomm.BatchCommit(batchHandle);

    // Block and wait for all completion records generated by the entire batch of tasks.
    hcomm.Drain(batchHandle);
}
```
