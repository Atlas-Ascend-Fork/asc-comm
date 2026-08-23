# MakeBatchHandle

## Function Description

Creates a protocol-specific batch handle and binds the UB buffer used to prepare batched WQEs. Batch handles currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

The interface maps the channel type to its corresponding BatchHandle return type. Use `auto` to receive the return value. `ChannelHandle` maps to the `UbcCtpBatchHandle` execution handle. `MultiChannelHandle` maps to `UbcCtpMultiBatchHandle`, which selects logical channels; call `GetHandleRef` to obtain its inner `UbcCtpBatchHandle` execution handle.

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

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | A UBC_CTP single-channel `ChannelHandle`, or a shared-Jetty `MultiChannelHandle` created by Host-side `MakeMultiChannelHandle`. |
| `buff` | Input | Caller-provided UB `LocalTensor` used to prepare batched WQEs. Batch `Drain` also reuses it as CQE scratch space. |
| `buffLen` | Input | Available buffer length in bytes. |
| `remoteAddr` | Input | Selects a remote registered buffer in single-channel mode. Reserved in multi-channel mode; use `GetHandleRef` to select a remote MR. Default: `nullptr`. |
| `localAddr` | Input | Local memory address. Its default value is `nullptr`. It is reserved in the current version and is not used to cache local registration information. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Communication channel handle type, deduced from `channel`. Currently supports `ChannelHandle` and `MultiChannelHandle`. |
| `U` | Element type of the `LocalTensor`. |

## Return Value

Returns `BatchHandle<T>`: `ChannelHandle` resolves to `UbcCtpBatchHandle`, and `MultiChannelHandle` resolves to `UbcCtpMultiBatchHandle`. This interface is supported only by `COMM_PROTOCOL_UBC_CTP`.

This interface does not report invalid arguments through a status code. The caller must satisfy the constraints below. Invalid arguments trigger an interface assertion in debug builds.

## Constraints

- Batch handles currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950. The `COMM_PROTOCOL_ROCE` batch path is not supported.
- In single-channel mode, `channel` must not be `0` and must point to an initialized UBC_CTP channel entity. A non-null `remoteAddr` must fall within a remote registered buffer; when null, the channel must contain at least one remote registered buffer. The interface caches the selected buffer's `tokenId/tokenValue`, and later batch operations do not query the remote registration table again.
- In multi-channel mode, `channel` must not be an empty `MultiChannelHandle`, and its shared queue entity and remote information array must be valid. `remoteAddr` is not used; call `GetHandleRef` to select a logical channel and remote MR.
- `localAddr` is not used for address validation or token caching in the current version.
- The start address of `buff` must be 32-byte aligned, and `buffLen` must be at least 64 bytes.
- `buffLen` must not exceed `buff.GetSize() * sizeof(U)`.
- `buffLen` must be strictly smaller than the SQ capacity, that is, `sqDepth * 64` bytes. The buffer can hold `buffLen / 64` WQEBBs; any remaining bytes are not used for WQEs.
- A BatchHandle caches SQ/CQ contexts and queue counters at creation time. The caller must exclusively own the single channel or shared Jetty while the BatchHandle is active. Do not mix ordinary `ChannelHandle` calls or use multiple BatchHandles concurrently.
- The caller manages the lifetime of the BatchHandle, channel resources, `MultiChannelHandle`, and `buff`. Do not release or concurrently reuse these resources before use of the BatchHandle is complete.
- Treat the BatchHandle as an opaque logical handle. Direct access to or modification of its members is not recommended.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>();
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen, remoteAddr);

auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferLen);
auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, peerRemoteAddr);
```

In multi-channel mode, call [GetHandleRef](./GetHandleRef.md) to select a logical channel and prepare WQEs through the returned `BatchHandle<ChannelHandle>` reference. Call `BatchCommit` and batch `Drain` on that inner reference.
Use `multiBatchHandle` only for subsequent `GetHandleRef` calls.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [GetHandleRef](./GetHandleRef.md)
