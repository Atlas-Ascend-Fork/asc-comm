# MakeBatchHandle

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
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

**Table 1**  Template Parameter Description

| Parameter | Description |
| --- | --- |
| `T` | Channel handle type. `ChannelHandle` and `MultiChannelHandle` are supported. |
| `U` | Element type of the `LocalTensor`. |

**Table 2**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | A UBC_CTP single-channel `ChannelHandle`, or a `MultiChannelHandle` created by Host-side `MakeMultiChannelHandle`. |
| `buff` | Input | Caller-provided UB `LocalTensor` used as the workspace for batch operations. |
| `buffLen` | Input | Available workspace length in bytes. |
| `remoteAddr` | Input | Selects remote registered memory in single-channel mode. If `nullptr`, the first remote registered buffer is selected. Ignored in multi-channel mode. Default: `nullptr`. |
| `localAddr` | Input | Reserved. Default: `nullptr`. |

## Return Value

Returns the `BatchHandle<T>` corresponding to `channel`. This API does not return a status code, so the caller must ensure that all parameters satisfy the constraints.

## Constraints

- Only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950 is supported. `COMM_PROTOCOL_ROCE` is not supported.
- `channel` must be valid and its communication resources must be initialized.
- In single-channel mode, a non-null `remoteAddr` must be within a remote registered buffer. Remote addresses used by subsequent batch operations must belong to that buffer.
- In multi-channel mode, use `GetHandleRef` to select a logical channel and remote registered memory.
- The start address of `buff` must be 32-byte aligned.
- `buffLen` must be at least 64 bytes, must not exceed `buff.GetSize() * sizeof(U)`, and must be smaller than `sqDepth * 64` bytes. Any remainder smaller than 64 bytes is unusable.
- The caller manages the lifetimes of the BatchHandle, channel resources, `MultiChannelHandle`, and `buff`. Do not release or concurrently reuse them before BatchHandle operations are complete.
- While using a BatchHandle, exclusively own its associated channel resources. Do not mix ordinary `ChannelHandle` operations or use another BatchHandle concurrently.
- Treat the BatchHandle as opaque. Do not access or modify its members directly.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // Initialized UBC_CTP channel handle
AscendC::LocalTensor<uint8_t> batchBuffer; // Allocated from UB memory
GM_ADDR remoteAddr; // Registered remote address
uint32_t batchBufferLen = 64U;

auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteAddr);
```

Multi-channel mode:

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::MultiChannelHandle multiChannel; // Valid multi-channel handle created on the Host
AscendC::LocalTensor<uint8_t> batchBuffer; // Allocated from UB memory
uint32_t batchBufferLen = 64U;

auto multiBatchHandle = hcomm.MakeBatchHandle(
    multiChannel, batchBuffer, batchBufferLen);
```
