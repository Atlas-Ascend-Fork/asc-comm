# MakeBatchHandle

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol but not the RoCE protocol
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Not supported
<!-- end id7 -->

## Function Description

Creates a batch handle that matches the communication protocol and binds the UB buffer used to prepare batch WQEs. Batch handles currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

The interface returns the corresponding BatchHandle type based on the handle type. It is recommended that the calling side use `auto` to receive the return value. `ChannelHandle` maps to the `UbcBatchHandle` execution handle; `MultiChannelHandle` maps to `UbcMultiBatchHandle`, which is used to select logical channels. Obtain its inner `UbcBatchHandle` execution handle through `GetHandleRef`.

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

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Channel handle type. Currently supports `ChannelHandle` and `MultiChannelHandle`. |
| `U` | Element type of the `LocalTensor`. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | A UBC_CTP single-channel `ChannelHandle`, or a shared-Jetty `MultiChannelHandle` created by the Host-side `MakeMultiChannelHandle`. |
| `buff` | Input | Caller-provided UB `LocalTensor` used to prepare batch WQEs, and reused by batch `Drain` as a CQE temporary buffer. |
| `buffLen` | Input | Available buffer length in bytes. |
| `remoteAddr` | Input | Used to select a remote registered buffer in single-channel mode. Reserved parameter in multi-channel mode. Default: `nullptr`. |
| `localAddr` | Input | Reserved parameter, not used in this version. Default: `nullptr`. |

## Return Value

Returns `BatchHandle<T>`. Only `COMM_PROTOCOL_UBC_CTP` supports this interface.

This interface does not report parameter errors through a return code. If channel or remote MR selection fails, a zero-initialized invalid handle is returned. The caller must ensure that the passed parameters satisfy the constraints below.

## Constraints

- Currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950. The `COMM_PROTOCOL_ROCE` batch handle is not supported.
- In single-channel mode, `channel` must not be `0` and must point to a UBC_CTP channel entity whose resources have been initialized. A non-null `remoteAddr` must fall within the range of a remote registered buffer; when null, the first buffer in the remote registration table is selected. The interface caches the `tokenId/tokenValue` of the selected buffer, and subsequent batch interfaces do not query the remote registration table again.
- In multi-channel mode, `multiChannel` must not be an empty `MultiChannelHandle`, and its shared queue entity and remote information array must be valid. Select the logical channel and remote MR through `GetHandleRef`.
- `localAddr` is not involved in address validation or token caching currently.
- The start address of `buff` must be 32-byte aligned, and `buffLen` must not be smaller than 64 bytes.
- `buffLen` must not exceed `buff.GetSize() * sizeof(U)`.
- `buffLen` must be strictly smaller than the channel SQ capacity, that is, `sqDepth * 64` bytes. The number of WQEBBs that can be prepared is `buffLen / 64`, and the remaining space that cannot be divided evenly is not used to store WQEs.
- A BatchHandle caches the SQ/CQ contexts and queue counters at creation time. While the BatchHandle is valid, the caller must exclusively own the corresponding single channel or shared Jetty, must not interleave ordinary `ChannelHandle` calls, and must not use multiple BatchHandles concurrently.
- The caller is responsible for the lifetime of the BatchHandle itself, the channel resources, the `MultiChannelHandle`, and `buff`. Before use of the BatchHandle is complete, these resources must not be released or concurrently reused.
- The BatchHandle should be used as a logically opaque handle. Direct access to or modification of its members by the caller is not recommended.

## Example

The calling example of MakeBatchHandle is as follows:

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>();
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen, remoteAddr);

auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferLen);
auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, peerRemoteAddr);
```
