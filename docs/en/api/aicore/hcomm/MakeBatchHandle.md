# MakeBatchHandle

## Function Description

Creates a batch handle and binds the UB workspace required by batch operations. This API currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

Use `auto` to receive the return value. In single-channel mode, use the returned handle to add and submit tasks. In multi-channel mode, call `GetHandleRef` to select a logical channel before adding tasks.

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
| `channel` | Input | A UBC_CTP single-channel `ChannelHandle`, or a `MultiChannelHandle` created by Host-side `MakeMultiChannelHandle`. |
| `buff` | Input | Caller-provided UB `LocalTensor` used as the workspace for batch operations. |
| `buffLen` | Input | Available workspace length in bytes. |
| `remoteAddr` | Input | Selects remote registered memory in single-channel mode. If `nullptr`, the first remote registered buffer is selected. Ignored in multi-channel mode. Default: `nullptr`. |
| `localAddr` | Input | Reserved; not used in the current version. Default: `nullptr`. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Channel handle type. `ChannelHandle` and `MultiChannelHandle` are supported. |
| `U` | Element type of the `LocalTensor`. |

## Return Value

Returns the `BatchHandle<T>` corresponding to `channel`. If the multi-channel handle is invalid or remote registered-memory selection fails, the API returns a zero-valued invalid batch handle, which must not be passed to subsequent batch APIs. This API does not return a status code, so the caller must ensure that all parameters satisfy the constraints.

## Constraints

- Only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950 is supported. `COMM_PROTOCOL_ROCE` is not supported.
- `channel` must be valid and its communication resources must be initialized.
- In single-channel mode, a non-null `remoteAddr` must be within a remote registered buffer. Remote addresses used by subsequent batch operations must belong to that buffer.
- In multi-channel mode, use `GetHandleRef` to select a logical channel and remote registered memory.
- The start address of `buff` must be 32-byte aligned.
- `buffLen` must be at least 64 bytes and must not exceed `buff.GetSize() * sizeof(U)`. Any remainder smaller than 64 bytes is unusable.
- The number of 64-byte task slots actually used by one batch must be smaller than the channel task-submission capacity; otherwise, `BatchCommit` returns `-1`. This capacity is determined when channel resources are created on the Host.
- The caller manages the lifetimes of the BatchHandle, channel resources, `MultiChannelHandle`, and `buff`. Do not release or concurrently reuse them before BatchHandle operations are complete.
- While using a BatchHandle, exclusively own its associated channel resources. Do not mix ordinary `ChannelHandle` operations or use another BatchHandle concurrently.
- Treat the BatchHandle as opaque. Do not access or modify its members directly.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>();

// Single channel
auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteAddr);

// Multiple channels
auto multiBatchHandle = hcomm.MakeBatchHandle(
    multiChannel, batchBuffer, batchBufferLen);
auto& peerBatchHandle = hcomm.GetHandleRef(
    multiBatchHandle, channelIndex, peerRemoteAddr);

// In multi-channel mode, use `peerBatchHandle` to add batch tasks and use `multiBatchHandle` with `BatchCommit` and batch `Drain`.
```

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [GetHandleRef](./GetHandleRef.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
