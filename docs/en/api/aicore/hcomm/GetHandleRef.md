# GetHandleRef

## Function Description

Returns the BatchHandle reference used for batch operations. A single-channel handle is returned unchanged; for a multi-channel handle, the call selects a logical channel and remote registered-memory region.

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

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. |
| `channelIndex` | Input | Multi-channel logical channel index. Ignored for a single-channel handle. |
| `remoteAddr` | Input | Address in a multi-channel remote registered buffer. When null, selects the first remote registered buffer of that channel. Ignored for a single-channel handle. Default: `nullptr`. |

## Return Value

Returns the input handle itself for a single channel, or the inner `BatchHandle<ChannelHandle>&` configured for the selected logical channel and remote MR for multiple channels.

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type deduced from `batchHandle`. |

## Constraints

- For a single-channel handle, `channelIndex` and `remoteAddr` do not modify the handle; remote memory remains selected by `MakeBatchHandle`.
- For a multi-channel handle, `channelIndex` must be smaller than the `channelNum` used to create the `MultiChannelHandle`.
- For a multi-channel handle, a non-null `remoteAddr` must belong to a remote registered buffer of the selected logical channel. When null, the first remote registered buffer of that channel is selected.
- This interface has no status return. The caller must provide a valid logical-channel index and remote-MR address for a multi-channel handle.
- Repeated calls on the same multi-channel batch handle return the same inner BatchHandle reference. Each call updates the logical-channel and remote MR information currently selected in that inner handle.
- Multi-channel remote buffers accessed through the returned reference must use the token cached by this call. Call this interface again before accessing a remote buffer that uses a different token.
- For multiple channels, use the returned inner reference with batch read, write, and write-with-notify APIs. Pass the outer multi-channel batch handle to `BatchCommit` and batch `Drain`.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
