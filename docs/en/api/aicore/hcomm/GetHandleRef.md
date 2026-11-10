# GetHandleRef

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:31:41.288Z pushedAt=2026-11-09T06:47:54.965Z nl=2 -->

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

Obtains a **BatchHandle** reference used to add batch tasks. In multi-channel mode, this API selects a logical channel and its remote registered memory. In single-channel mode, it returns the input handle itself.

## Function Prototype

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline BatchHandle<T>& GetHandleRef(
    T& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr = nullptr);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type, deduced from the `batchHandle` argument. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by [MakeBatchHandle](./MakeBatchHandle.md). |
| `channelIndex` | Input | Logical channel index in multi-channel mode; ignored in single-channel mode. |
| `remoteAddr` | Input | Used to select remote registered memory in multi-channel mode. When `nullptr` is passed, the first remote registered memory of the logical channel is selected; ignored in single-channel mode. The default value is `nullptr`. |

## Return Value

Returns a **BatchHandle** reference used to add batch tasks.

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- In multi-channel mode, `channelIndex` must be less than the `channelNum` specified when creating the `MultiChannelHandle` through [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md).
- In multi-channel mode, when `remoteAddr` is not null, it must be within a remote registered memory of the selected logical channel. Batch operations initiated through the returned handle must access that registered memory.
- Call this API again when switching the logical channel or remote registered memory.
- Repeated calls for the same multi-channel batch handle return references to the same inner **BatchHandle**. Each call updates the logical channel and remote registered memory currently selected by that handle. After another call, a previously saved reference also represents the new selection and must not be used as if it retained the old selection.
- This API does not return a status code. The caller must ensure that the logical channel index and remote address are valid.
- In multi-channel mode, call the batch read, write, and write-notify APIs through the returned handle reference; call [BatchCommit](./BatchCommit.md) and batch [Drain](./Drain.md) through the multi-channel batch handle returned by [MakeBatchHandle](./MakeBatchHandle.md).
- In single-channel mode, `channelIndex` and `remoteAddr` have no effect. Remote registered memory is selected by `MakeBatchHandle`.
- Under the `COMM_PROTOCOL_ROCE` protocol, this API is a pass-through implementation that returns the input batch handle itself, and `channelIndex` and `remoteAddr` have no effect. Multi-channel batching is supported only by the `COMM_PROTOCOL_UB_CTP` protocol; `COMM_PROTOCOL_ROCE` does not support multi-channel mode.

## Call Example

The following shows an example of calling the **GetHandleRef** API. To focus on the call flow, the example omits return value checks and error handling. For the complete sample, see [02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md).

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Maximum number of logical channels, adjusted based on the number of channels created on the host side. channelNum must not exceed this value.
constexpr uint32_t MAX_CHANNEL_NUM = 8U;
// Batch workspace size: divided into 64-byte task slots, and must accommodate one write task per logical channel (the total number of slots must be less than the channel's task submission capacity).
constexpr uint32_t BATCH_BUF_SIZE = MAX_CHANNEL_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_multi_channel_write_kernel(
    uint64_t multiChannel, GM_ADDR localBuffer, __gm__ uint64_t* remoteBuffers,
    uint32_t channelNum)
{
    // Declare the batch UB workspace. The batch API does not depend on Init. alignas(32) ensures that the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // Create an Hcomm object.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // Construct the batch workspace LocalTensor from the start address of the UB workspace.
    AscendC::LocalTensor<uint8_t> batchBuffer(
        AscendC::TPosition::VECOUT,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

    // Create a multi-channel batch handle.
    auto multiBatchHandle = hcomm.MakeBatchHandle(
        multiChannel, batchBuffer, BATCH_BUF_SIZE);

    // Obtain the remote registered memory address for each logical channel, select the logical channel, and add a write task through the returned handle reference.
    for (uint32_t channelIndex = 0U; channelIndex < channelNum; ++channelIndex) {
        GM_ADDR peerRemoteAddr = reinterpret_cast<GM_ADDR>(remoteBuffers[channelIndex]);
        auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, peerRemoteAddr);
        hcomm.WriteNbi(peerBatchHandle, peerRemoteAddr, localBuffer, BYTES);
    }

    // Submit and wait for all tasks to complete through the multi-channel batch handle.
    hcomm.BatchCommit(multiBatchHandle);
    hcomm.Drain(multiBatchHandle);
}
```
