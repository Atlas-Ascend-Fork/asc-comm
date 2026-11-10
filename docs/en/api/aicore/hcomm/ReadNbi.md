# ReadNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:38:22.718Z pushedAt=2026-11-09T06:47:54.970Z nl=2 -->

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

Reads data from the remote `src` to the local `dst`. The API provides a common `channel` overload and a batch `batchHandle` overload: the common overload directly adds a read task to the channel; the batch overload adds the read task to the current batch, which is later committed by [BatchCommit](./BatchCommit.md).

## Function Prototype

Common API:

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t ReadNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

Batch API:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t ReadNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

Batch multi-SGE (Scatter-Gather Element) API:

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t ReadNbi(T& batchHandle, const BufDesc* dstDescs, uint32_t dstNum, GM_ADDR src);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether to commit immediately when the common API submits a task. The batch API does not provide this parameter. |
| `commitPipe` | Pipe used by the common API for commit, defaulting to `PIPE_S`. |
| `reqPipe` | Pipe used by the common API for requests, defaulting to `PIPE_MTE3`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order execution + in-order CQE reporting + fence enabled + CQE reporting required); under the `COMM_PROTOCOL_ROCE` protocol, only the `cqe` and `fence` fields take effect. |
| `T` | Batch handle type, deduced from the `batchHandle` argument. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the common API. |
| `batchHandle` | Input/Output | Single-channel batch handle created by [MakeBatchHandle](./MakeBatchHandle.md), or the **BatchHandle** reference returned by [GetHandleRef](./GetHandleRef.md). |
| `dst` | Output | Absolute address of the local destination GM (Global Memory). |
| `src` | Input | Absolute address of the remote source GM. |
| `len` | Input | Read length in bytes. Under the `COMM_PROTOCOL_UB_CTP` protocol, it does not exceed 256 MB (256 * 1024 * 1024 bytes); under the `COMM_PROTOCOL_ROCE` protocol, it is less than 2 GB (2 * 1024 * 1024 * 1024 bytes). |
| `dstDescs` | Input | Array of local scatter memory segment descriptors (multi-SGE API), where each element contains `addr` (segment start GM address) and `len` (segment length in bytes). |
| `dstNum` | Input | Number of memory segments (multi-SGE API). Under the `COMM_PROTOCOL_UB_CTP` protocol, the value ranges from 1 to 13; under the `COMM_PROTOCOL_ROCE` protocol, from 1 to 24. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | The normal task is submitted successfully, or the read task is successfully added to the current batch. |
| `-1` | The operation fails. For the batch API, this includes insufficient remaining space in the batch workspace. |

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- `len` is the transfer length in bytes: under the `COMM_PROTOCOL_UB_CTP` protocol, it does not exceed 256MB (256 * 1024 * 1024 bytes); under the `COMM_PROTOCOL_ROCE` protocol, it is less than 2GB (2 * 1024 * 1024 * 1024 bytes).

**Common API**

- Before the call, the communication channel must be initialized, and a temporary workspace must be provided through `Init`.
- Under the `COMM_PROTOCOL_UB_CTP` protocol, `src` must fall within the remote registered memory range registered by the channel, and `dst` is the local destination address.
- A single `ReadNbi` task occupies one task slot (64 bytes). When the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred-commit tasks must not exceed `sqDepth`. The accumulated tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise, subsequent task submission fails due to SQ overflow.
- In the deferred-commit scenario (multiple deferred commits + one final commit), only the final commit should generate a completion record (that is, `config.cqe` of intermediate tasks is set to 0, and that of the final task is set to 1).

**Batch API**

- Each batch read task occupies one task slot (64 bytes) of batch workspace.
- For a batch handle, `[src, src + len)` must belong to its selected remote registered memory.
- Returns `-1` when the remaining space in the batch workspace is insufficient.
- After adding tasks, call [BatchCommit](./BatchCommit.md). In multi-channel mode, add tasks through the handle reference returned by [GetHandleRef](./GetHandleRef.md), and commit them through the multi-channel batch handle returned by [MakeBatchHandle](./MakeBatchHandle.md). The associated channel resources must be exclusively occupied during use.
- Multi-channel batch is supported only under the `COMM_PROTOCOL_UB_CTP` protocol. `COMM_PROTOCOL_ROCE` does not support multi-channel mode.
- Both the `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols support the multi-SGE API. In the multi-SGE API, data read from the remote `src` is written to the discrete local memory segments specified by `dstDescs`, with the total length equal to the sum of the `len` of each segment. The value range of `dstNum` is 1-13 under the `COMM_PROTOCOL_UB_CTP` protocol and 1-24 under the `COMM_PROTOCOL_ROCE` protocol. Under the `COMM_PROTOCOL_UB_CTP` protocol, the task addition API returns `-1` when the value is out of range; under the `COMM_PROTOCOL_ROCE` protocol, exceeding the limit triggers a debug-time assertion. Under the `COMM_PROTOCOL_ROCE` protocol, the `len` of each segment and the accumulated total length must both be less than 2 GB (2 * 1024 * 1024 * 1024 bytes).

## Call Example

The following shows an example of calling the **ReadNbi** API. To focus on the call flow, the example omits return value checks and error handling. For the complete sample, see [01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md).

Common API:

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UB temporary workspace size: it must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_read_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace. alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Read data from the remote remoteBuffer to the local localBuffer.
    hcomm.ReadNbi(channel, localBuffer, remoteBuffer, BYTES);

    // Block until the read task completes.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Number of read tasks added in a single batch.
constexpr uint32_t TASK_NUM = 4U;
// Batch workspace size: divided into 64-byte task slots, it must accommodate the slots of the entire batch (the total number of slots must be less than the channel's task submission capacity).
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_read_nbi_kernel(
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

    // Create a single-channel batch handle and bind it to the batch workspace. remoteBuffer is used to select the remote registered memory.
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // Add TASK_NUM read tasks to the current batch in sequence, reading from different remote offsets to different local offsets. The tasks are first accumulated in the batch workspace and not yet committed.
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.ReadNbi(batchHandle, localBuffer + i * BYTES, remoteBuffer + i * BYTES, BYTES);
    }

    // Commit all tasks accumulated in the current batch at once.
    hcomm.BatchCommit(batchHandle);

    // Block and wait for all completion records generated by the entire batch of tasks.
    hcomm.Drain(batchHandle);
}
```
