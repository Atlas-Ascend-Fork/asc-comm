# GetHandleRef

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

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. |
| `channelIndex` | Input | Logical channel index in multi-channel mode. Ignored in single-channel mode. |
| `remoteAddr` | Input | Selects remote registered memory in multi-channel mode. If `nullptr`, the first remote registered buffer of the logical channel is selected. Ignored in single-channel mode. Default: `nullptr`. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type deduced from `batchHandle`. |

## Return Value

Returns a BatchHandle reference used to add batch tasks.

## Constraints

- In multi-channel mode, `channelIndex` must be smaller than the `channelNum` specified when creating the `MultiChannelHandle`.
- In multi-channel mode, a non-null `remoteAddr` must be within a remote registered buffer of the selected logical channel. Batch operations through the returned handle must access that buffer.
- Call this API again when switching to another logical channel or remote registered buffer.
- Repeated calls for the same multi-channel batch handle return references to the same inner BatchHandle. Each call updates the logical channel and remote registered memory currently selected by that handle. After another call, a previously saved reference also represents the new selection and must not be used as if it retained the old selection.
- This API does not return a status code. The caller must ensure that the logical channel index and remote address are valid.
- In multi-channel mode, use the returned handle reference with batch read, write, and write-with-notify APIs. Use the multi-channel batch handle returned by `MakeBatchHandle` with `BatchCommit` and batch `Drain`.
- In single-channel mode, `channelIndex` and `remoteAddr` have no effect. Remote registered memory is selected by `MakeBatchHandle`.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
