# GetHandleRef

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

Returns a BatchHandle reference used to add batch tasks. In multi-channel mode, this API selects a logical channel and its remote registered memory. In single-channel mode, it returns the input handle itself.

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

## Parameter Description

**Table 1**  Template Parameter Description

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type deduced from `batchHandle`. |

**Table 2**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. |
| `channelIndex` | Input | Logical channel index in multi-channel mode. Ignored in single-channel mode. |
| `remoteAddr` | Input | Selects remote registered memory in multi-channel mode. If `nullptr`, the first remote registered buffer of the logical channel is selected. Ignored in single-channel mode. Default: `nullptr`. |

## Return Value

Returns a BatchHandle reference used to add batch tasks.

## Constraints

- In multi-channel mode, `channelIndex` must be smaller than the `channelNum` specified when creating the `MultiChannelHandle`.
- In multi-channel mode, a non-null `remoteAddr` must be within a remote registered buffer of the selected logical channel. Batch operations through the returned handle must access that buffer.
- Call this API again when switching to another logical channel or remote registered buffer.
- This API does not return a status code. The caller must ensure that the logical channel index and remote address are valid.
- In multi-channel mode, use the returned handle reference with batch read, write, and write-with-notify APIs. Use the multi-channel batch handle returned by `MakeBatchHandle` with `BatchCommit` and batch `Drain`.
- In single-channel mode, `channelIndex` and `remoteAddr` have no effect. Remote registered memory is selected by `MakeBatchHandle`.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::MultiChannelHandle multiChannel; // Valid multi-channel handle created on the Host
AscendC::LocalTensor<uint8_t> batchBuffer; // Allocated from UB memory
GM_ADDR remoteDst; // Registered remote destination address of the selected logical channel
GM_ADDR localSrc; // Valid local source address
uint32_t batchBufferLen = 64U;
uint32_t channelIndex = 0U;
uint32_t dataLen = 64U;

auto multiBatchHandle = hcomm.MakeBatchHandle(
    multiChannel, batchBuffer, batchBufferLen);
auto& batchHandle = hcomm.GetHandleRef(
    multiBatchHandle, channelIndex, remoteDst);
int32_t ret = hcomm.WriteNbi(batchHandle, remoteDst, localSrc, dataLen);
if (ret == 0) {
    ret = hcomm.BatchCommit(multiBatchHandle);
}
if (ret == 0) {
    ret = hcomm.Drain(multiBatchHandle);
}
```
