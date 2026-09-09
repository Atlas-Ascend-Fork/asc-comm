# GetHandleRef

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

Returns the BatchHandle reference used to perform batch operations. For a single channel, the passed handle is returned unchanged; for multiple channels, a logical channel and remote registered memory are selected.

## Function Prototype

```cpp
template <
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    T& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr = nullptr);
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type deduced from the `batchHandle` argument. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. |
| `channelIndex` | Input | Multi-channel logical channel index. Ignored for a single channel. |
| `remoteAddr` | Input | Address within a remote registered buffer in multi-channel mode. When null, the first remote registered buffer of that channel is selected. Ignored for a single channel. Default: `nullptr`. |

## Return Value

For a single channel, the input handle itself is returned; for multiple channels, the inner `BatchHandle<ChannelHandle>&` configured with the selected logical channel and remote MR is returned.

## Constraints

- For a single channel, `channelIndex` and `remoteAddr` do not change the handle, and remote memory is still selected by `MakeBatchHandle`.
- For multiple channels, `channelIndex` must be smaller than the `channelNum` passed when the `MultiChannelHandle` was created.
- For multiple channels, a non-null `remoteAddr` must fall within a remote registered buffer of the selected logical channel; when null, the first remote registered buffer of that channel is selected.
- This interface does not return a status code. The caller must ensure that the logical channel index and remote MR address of the multi-channel handle are valid.
- When this interface is called multiple times on the same multi-channel batch handle, the returned references all point to the same inner BatchHandle. Each call updates the logical channel and remote MR information currently selected in that inner handle.
- Batch operations on the multi-channel returned reference must use the token cached by the current call. Call this interface again before accessing a remote buffer that uses a different token.
- For multiple channels, use the returned inner reference to call batch read, write, and write-with-notify; pass the outer multi-channel batch handle to `BatchCommit` and batch `Drain`.

## Example

The calling example of GetHandleRef is as follows:

```cpp
auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferLen);
auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, peerRemoteAddr);
hcomm.WriteNbi(peerBatchHandle, dst, src, len);    // Prepare WQEs through the inner reference
hcomm.BatchCommit(multiBatchHandle);    // Commit through the outer handle
hcomm.Drain(multiBatchHandle);    // Wait for completion through the outer handle
```
