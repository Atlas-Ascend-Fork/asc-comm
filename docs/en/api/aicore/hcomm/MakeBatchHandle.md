# MakeBatchHandle

## Function Description

Creates a protocol-specific batch handle and binds the UB buffer used to prepare batched WQEs. Batch handles currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

The concrete handle type is selected by the protocol template argument of `Hcomm`. Use `auto` to receive the return value.

## Function Prototype

```cpp
__aicore__ inline BatchHandleType MakeBatchHandle(
    AscendC::ChannelHandle channel,
    GM_ADDR dstBase,
    GM_ADDR srcBase,
    __ubuf__ uint8_t* buff,
    uint32_t buffLen);

template <typename T>
__aicore__ inline BatchHandleType MakeBatchHandle(
    AscendC::ChannelHandle channel,
    GM_ADDR dstBase,
    GM_ADDR srcBase,
    const AscendC::LocalTensor<T>& buff,
    uint32_t buffLen);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | UBC_CTP communication channel handle. |
| `dstBase` | Input | Base address of remote registered memory bound to the batch handle. The interface finds and caches the remote token using this address. |
| `srcBase` | Input | Base address of local source memory. It is reserved in the current version and is not used to cache local registration information. |
| `buff` | Input | Caller-provided UB buffer or `LocalTensor` used to prepare batched WQEs. Batch `Drain` also reuses it as CQE scratch space. |
| `buffLen` | Input | Available buffer length in bytes. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Element type of the `LocalTensor`. Used only by the `LocalTensor` overload. |

## Return Value

Returns the `BatchHandleType` selected by `commProtocol`. `COMM_PROTOCOL_UBC_CTP` maps to `UbcCtpBatchHandle`.

This interface does not report invalid arguments through a status code. The caller must satisfy the constraints below. Invalid arguments trigger an interface assertion in debug builds.

## Constraints

- Batch handles currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950. The `COMM_PROTOCOL_ROCE` batch path is not supported.
- `channel` must not be `0` and must point to an initialized UBC_CTP channel entity.
- `dstBase` must fall within a remote registered buffer of `channel`. The interface caches the token of that buffer, and subsequent batch read/write calls do not query the remote registration table again.
- `srcBase` is not used for address validation or token caching in the current version.
- The start address of `buff` must be 32-byte aligned, and `buffLen` must be at least 64 bytes.
- For the `LocalTensor` overload, `buffLen` must not exceed `buff.GetSize() * sizeof(T)`.
- `buffLen` must be strictly smaller than the SQ capacity, that is, `sqDepth * 64` bytes. The buffer can hold `buffLen / 64` WQEBBs; any remaining bytes are not used for WQEs.
- A BatchHandle caches SQ/CQ contexts and queue counters at creation time. The caller must exclusively own the channel while the BatchHandle is active. Do not mix ordinary `ChannelHandle` calls on the same channel or use multiple BatchHandles concurrently on that channel.
- The caller manages the lifetime of the BatchHandle, channel resources, and `buff`. Do not release or concurrently reuse these resources before use of the BatchHandle is complete.
- Treat the BatchHandle as an opaque logical handle. Direct access to or modification of its members is not recommended.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
auto batchHandle = hcomm.MakeBatchHandle(channel, dstBase, srcBase, batchBuffer, batchBufferLen);
```
