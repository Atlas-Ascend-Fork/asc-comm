# GetHandleRef

## Function Description

Selects one logical channel from a shared-Jetty multi-channel batch handle and returns a channel reference for batch read, write, or write-with-notify APIs.

References for different logical channels share batch queue state in the parent handle and are submitted and drained together through that parent handle.

## Function Prototype

```cpp
template <typename T>
__aicore__ inline BatchHandleRef<typename BatchHandleTraits<T>::ChannelType>
GetHandleRef(T& batchHandle, uint32_t channelIndex);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Multi-channel batch handle created by `MakeBatchHandle(MultiChannelHandle, ...)`. The returned reference points to its shared batch state. |
| `channelIndex` | Input | Logical channel index. It matches the index in the `channelDescs` array passed to Host-side `MakeMultiChannelHandle`. |

## Return Value

Returns `BatchHandleRef<MultiChannelHandle>`. Its current concrete type is `UbcCtpBatchHandleRef`.

If the parent handle is invalid, the UB buffer capacity is 0, `channelIndex` is out of range, or the remote information address is invalid, the interface returns an invalid reference whose shared-state pointer is `nullptr`.

## Constraints

- `batchHandle` must be created from a valid `MultiChannelHandle`; do not pass a single-channel `UbcCtpBatchHandle`.
- The return value refers to shared state in the parent handle, so the parent handle and its UB buffer must remain valid. Do not prepare WQEs concurrently through different channel references.
- Pass the parent multi-channel batch handle, not this return value, to `BatchCommit` and `Drain`.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
