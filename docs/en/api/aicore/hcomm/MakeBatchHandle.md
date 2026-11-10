# MakeBatchHandle

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:35:16.590Z pushedAt=2026-11-09T06:47:54.969Z nl=2 -->

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

Creates a batch handle and binds the UB workspace required for batch operations. The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.

The caller is advised to use `auto` to receive the return value. In single-channel mode, use the returned handle directly to add and submit tasks. To submit tasks to multiple channels, create a batch handle for each channel. In multi-channel mode, use [GetHandleRef](./GetHandleRef.md) to select a logical channel before adding tasks. Multiple logical channels share the same batch handle, batch workspace, and underlying communication resources. Batch tasks are submitted through [BatchCommit](./BatchCommit.md) and awaited through [Drain](./Drain.md).

## Function Prototype

```cpp
template <typename T, typename U>
__aicore__ inline BatchHandle<T> MakeBatchHandle(
    T channel,
    const AscendC::LocalTensor<U>& buff,
    uint32_t buffLen,
    GM_ADDR remoteAddr = nullptr,
    GM_ADDR localAddr = nullptr);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Channel handle type, supporting `ChannelHandle` and `MultiChannelHandle`. |
| `U` | Element type of `LocalTensor`. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Single-channel `ChannelHandle` (supported by both `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols), or `MultiChannelHandle` created by [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md) on the host side. |
| `buff` | Input | UB `LocalTensor` provided by the caller, used as the workspace for batch operations. |
| `buffLen` | Input | Available length of the workspace, in bytes. |
| `remoteAddr` | Input | In single-channel mode, used to select the remote registered memory. When `nullptr` is passed, the first remote registered memory is selected. Ignored in multi-channel mode. The default value is `nullptr`. |
| `localAddr` | Input | Reserved parameter, not used in the current version. The default value is `nullptr`. |

## Return Value

Returns the `BatchHandle<T>` corresponding to `channel`. If the multi-channel handle is invalid or remote registered memory fails to be selected, the API returns a zero-valued invalid batch handle, which must not be passed to subsequent batch APIs. This API does not return a status code, so the caller must ensure that all parameters satisfy the constraints.

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- `channel` must be valid and its communication resources must be initialized.
- In single-channel mode, when `remoteAddr` is non-null, it must be located within a remote registered memory. Remote addresses accessed by subsequent batch operations must belong to that registered memory.
- In multi-channel mode, use `GetHandleRef` to select a logical channel and remote registered memory.
- The start address of `buff` must be 32-byte aligned.
- `buffLen` must be at least 64 bytes and must not exceed `buff.GetSize() * sizeof(U)`. Any remainder smaller than 64 bytes is unusable.
- The number of 64-byte task slots actually occupied by a batch must not exceed the number of batch workspace slots derived from `buffLen`; otherwise, the corresponding batch task addition APIs ([WriteNbi](./WriteNbi.md), [ReadNbi](./ReadNbi.md), [WriteWithNotifyNbi](./WriteWithNotifyNbi.md), and so on) return `-1` (under the `COMM_PROTOCOL_UB_CTP` protocol, this is API validation behavior; under the `COMM_PROTOCOL_ROCE` protocol, this overrun is a debug-time assertion). It must also be less than the task submission capacity of the channel. When the capacity is insufficient, `BatchCommit` first waits for in-flight tasks to complete to free up space. The task submission capacity is the SQ depth (sqDepth) of the channel, which is determined when channel resources are created on the host side.
- The caller manages the lifetimes of the **BatchHandle**, channel resources, `MultiChannelHandle`, and `buff`. Do not release or concurrently reuse them before **BatchHandle** operations are complete.
- While using a **BatchHandle**, exclusively own its associated channel resources. Do not mix common `ChannelHandle` operations or use another **BatchHandle** concurrently.
- Treat the **BatchHandle** as opaque. Do not access or modify its members directly.
- Under the `COMM_PROTOCOL_ROCE` protocol, the batch handle type is `UbcBatchHandle`, and multi-channel selection is not supported. Multi-channel batching is supported only by the `COMM_PROTOCOL_UB_CTP` protocol. `COMM_PROTOCOL_ROCE` does not support multi-channel mode, and [GetHandleRef](./GetHandleRef.md) is a pass-through implementation.

## Call Example

The following is an example of calling the **MakeBatchHandle** API. For complete samples, see [02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md) and [08_hcomm_roce_batch_write_read](../../../../../examples/aicore/hcomm/08_hcomm_roce_batch_write_read/README.md) (`COMM_PROTOCOL_ROCE` protocol).

```cpp
// Precondition: The following symbols are provided by the calling environment.
// channel: single-channel handle (ChannelHandle); multiChannel: multi-channel handle created by MakeMultiChannelHandle on the host side.
// remoteAddr / peerRemoteAddr: address in the remote registered memory (GM_ADDR); channelIndex: logical channel index (uint32_t).
// Batch workspace size: divided into 64-byte task slots, with the start address aligned to 32 bytes.
constexpr uint32_t BATCH_BUF_SIZE = 128U;
alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

// Construct the batch workspace LocalTensor from the UB workspace start address.
AscendC::LocalTensor<uint8_t> batchBuffer(
    AscendC::TPosition::VECOUT,
    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

// Single-channel mode: create a batch handle and bind the batch workspace. remoteAddr selects the remote registered memory.
auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, BATCH_BUF_SIZE, remoteAddr);

// Multi-channel mode: create a multi-channel batch handle, and select the logical channel and remote registered memory through GetHandleRef.
auto multiBatchHandle = hcomm.MakeBatchHandle(
    multiChannel, batchBuffer, BATCH_BUF_SIZE);
auto& peerBatchHandle = hcomm.GetHandleRef(
    multiBatchHandle, channelIndex, peerRemoteAddr);

// In multi-channel mode, use `peerBatchHandle` to add batch tasks and use `multiBatchHandle` with `BatchCommit` and batch `Drain`.
```
