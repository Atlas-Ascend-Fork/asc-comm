# MakeBatchHandle

## Function Description

Creates a protocol-specific batch handle and binds the UB buffer used to prepare batched WQEs. Batch handles currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

The interface maps the channel type to its corresponding BatchHandle return type. Use `auto` to receive the return
value. The current `ChannelHandle` mapping resolves to `UbcCtpBatchHandle`.

## Function Prototype

```cpp
template <typename T>
__aicore__ inline BatchHandle<T> MakeBatchHandle(
    T channel,
    __ubuf__ uint8_t* buff,
    uint32_t buffLen,
    GM_ADDR dstBase = nullptr,
    GM_ADDR srcBase = nullptr);

template <typename T, typename U>
__aicore__ inline BatchHandle<T> MakeBatchHandle(
    T channel,
    const AscendC::LocalTensor<U>& buff,
    uint32_t buffLen,
    GM_ADDR dstBase = nullptr,
    GM_ADDR srcBase = nullptr);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | UBC_CTP communication channel handle. |
| `buff` | Input | Caller-provided UB buffer or `LocalTensor` used to prepare batched WQEs. Batch `Drain` also reuses it as CQE scratch space. |
| `buffLen` | Input | Available buffer length in bytes. |
| `dstBase` | Input | Base address of remote registered memory bound to the batch handle. Its default value is `nullptr`. The interface finds and caches the remote token using this address; the current UBC_CTP implementation requires it to match a remote registered buffer. |
| `srcBase` | Input | Base address of local source memory. Its default value is `nullptr`. It is reserved in the current version and is not used to cache local registration information. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Communication channel handle type, deduced from `channel`. Currently supports `ChannelHandle`. |
| `U` | Element type of the `LocalTensor`. Used only by the `LocalTensor` overload. |

## Return Value

Returns `BatchHandle<T>`. The actual return type for `ChannelHandle` is currently `UbcCtpBatchHandle`. This
interface is supported only by `COMM_PROTOCOL_UBC_CTP`.

This interface does not report invalid arguments through a status code. The caller must satisfy the constraints below. Invalid arguments trigger an interface assertion in debug builds.

## Constraints

- Batch handles currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950. The `COMM_PROTOCOL_ROCE` batch path is not supported.
- `channel` must not be `0` and must point to an initialized UBC_CTP channel entity.
- `dstBase` must fall within a remote registered buffer of `channel`. The interface caches the token of that buffer, and subsequent batch read/write calls do not query the remote registration table again.
- `srcBase` is not used for address validation or token caching in the current version.
- The start address of `buff` must be 32-byte aligned, and `buffLen` must be at least 64 bytes.
- For the `LocalTensor` overload, `buffLen` must not exceed `buff.GetSize() * sizeof(U)`.
- `buffLen` must be strictly smaller than the SQ capacity, that is, `sqDepth * 64` bytes. The buffer can hold `buffLen / 64` WQEBBs; any remaining bytes are not used for WQEs.
- A BatchHandle caches SQ/CQ contexts and queue counters at creation time. The caller must exclusively own the channel while the BatchHandle is active. Do not mix ordinary `ChannelHandle` calls on the same channel or use multiple BatchHandles concurrently on that channel.
- The caller manages the lifetime of the BatchHandle, channel resources, and `buff`. Do not release or concurrently reuse these resources before use of the BatchHandle is complete.
- Treat the BatchHandle as an opaque logical handle. Direct access to or modification of its members is not recommended.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen, dstBase);
```
