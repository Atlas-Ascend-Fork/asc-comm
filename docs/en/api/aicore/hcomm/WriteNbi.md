# WriteNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:57:44.510Z pushedAt=2026-11-09T06:47:54.979Z nl=2 -->

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

Writes data from the local `src` to the remote `dst`. The API provides a common `channel` overload and a batch `batchHandle` overload: the common overload directly adds a write task to the channel; the batch overload adds the write task to the current batch, which is later committed uniformly by [BatchCommit](./BatchCommit.md).

## Function Prototype

Common API:

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

Batch API:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

Batch multi-SGE (Scatter-Gather Element) API:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteNbi(T& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the common API commits immediately when submitting the task. The batch API does not provide this parameter. |
| `commitPipe` | Pipe used by the common API for commit, defaulting to `PIPE_S`. |
| `reqPipe` | Pipe used by the common API for requests, defaulting to `PIPE_MTE3`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order execution + CQE in-order reporting + fence enabled + CQE reporting required). Under the `COMM_PROTOCOL_ROCE` protocol, only the `cqe` and `fence` fields take effect. |
| `T` | Batch handle type, deduced from the `batchHandle` argument. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the common API. |
| `batchHandle` | Input/Output | Single-channel batch handle created by [MakeBatchHandle](./MakeBatchHandle.md), or the **BatchHandle** reference returned by [GetHandleRef](./GetHandleRef.md). |
| `dst` | Output | Absolute remote GM (Global Memory) address of the destination. |
| `src` | Input | Absolute local source GM address. |
| `len` | Input | Write length, in bytes. Under the `COMM_PROTOCOL_UB_CTP` protocol, it does not exceed 256 MB (256 * 1024 * 1024 bytes); under the `COMM_PROTOCOL_ROCE` protocol, it is less than 2 GB (2 * 1024 * 1024 * 1024 bytes). |
| `srcDescs` | Input | Local scatter memory segment descriptor array (multi-SGE API). Each element contains `addr` (segment start GM address) and `len` (segment length, in bytes). |
| `srcNum` | Input | Number of memory segments (multi-SGE API). Under the `COMM_PROTOCOL_UB_CTP` protocol, the value ranges from 1 to 13; under the `COMM_PROTOCOL_ROCE` protocol, from 1 to 24. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | The common task is submitted successfully, or the write task is successfully added to the current batch. |
| `-1` | The operation fails. For the batch API, this includes insufficient remaining space in the batch workspace. |

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- `len` is the transfer length in bytes: no more than 256 MB (256 * 1024 * 1024 bytes) under the `COMM_PROTOCOL_UB_CTP` protocol; less than 2 GB (2 * 1024 * 1024 * 1024 bytes) under the `COMM_PROTOCOL_ROCE` protocol.

**Common API**

- Before the call, the communication channel must have been initialized, and a temporary workspace must be provided through `Init`.
- Under the `COMM_PROTOCOL_UB_CTP` protocol, `dst` must fall within the remote registered memory range registered for the channel, and `src` is the local source address.
- A single `WriteNbi` task occupies one task slot (64 bytes). When the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred-commit tasks must not exceed `sqDepth`. The accumulated tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise, subsequent task submission will fail due to SQ overflow.
- In the deferred-commit scenario (multiple deferred commits + one final commit), only the final commit should generate a completion record (that is, `config.cqe` of intermediate tasks is set to **0**, and that of the final task is set to **1**).

**Batch API**

- For a batch handle, `[dst, dst + len)` must belong to its selected remote registered memory.
- After the addition is complete, call [BatchCommit](./BatchCommit.md). In multi-channel mode, add tasks through the handle reference returned by [GetHandleRef](./GetHandleRef.md), and commit them through the multi-channel batch handle returned by [MakeBatchHandle](./MakeBatchHandle.md). The associated channel resources must be exclusively occupied during use.
- Multi-channel batch is supported only under the `COMM_PROTOCOL_UB_CTP` protocol. `COMM_PROTOCOL_ROCE` does not support multi-channel mode.
- Both the `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols support the multi-SGE API. In the multi-SGE API, the data written to the remote `dst` comes from the discrete memory segments specified by `srcDescs`, and the total length is the sum of the `len` of each segment. The value range of `srcNum` is 1-13 under the `COMM_PROTOCOL_UB_CTP` protocol and 1-24 under the `COMM_PROTOCOL_ROCE` protocol. Under the `COMM_PROTOCOL_UB_CTP` protocol, the task addition API returns `-1` when the value is out of range; under the `COMM_PROTOCOL_ROCE` protocol, exceeding the limit triggers a debug-time assertion. Under the `COMM_PROTOCOL_ROCE` protocol, the `len` of each segment and the accumulated total length must both be less than 2 GB (2 * 1024 * 1024 * 1024 bytes).

## Call Example

The following shows an example of calling the **WriteNbi** API. To focus on the call flow, the example omits return value checks and error handling. For the complete sample, see [01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md).

Common API:

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UB temporary workspace size: it must not be less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace. alignas(32) ensures that the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Write the local localBuffer data to the remote remoteBuffer.
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // Block and wait for the write task to complete.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Number of write tasks added in a single batch.
constexpr uint32_t TASK_NUM = 4U;
// Batch workspace size: divided into 64-byte task slots, and must accommodate slots for the entire batch (the total number of slots must be less than the channel's task submission capacity).
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Declare the batch UB workspace. The batch API does not depend on Init. alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // Create an Hcomm object.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // Construct the batch workspace LocalTensor from the start address of the UB workspace.
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
