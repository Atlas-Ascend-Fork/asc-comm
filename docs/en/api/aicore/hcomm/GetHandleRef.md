# GetHandleRef

## Function Description

Selects one logical channel from a shared-Jetty multi-channel batch handle and returns a BatchHandle reference for
batch read, write, or write-with-notify APIs.

Call `BatchCommit` and batch `Drain` on the parent multi-channel batch handle.

## Function Prototype

```cpp
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    BatchHandle<MultiChannelHandle>& batchHandle,
    uint32_t channelIndex);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Multi-channel batch handle created by `MakeBatchHandle(MultiChannelHandle, ...)`. |
| `channelIndex` | Input | Logical channel index. It matches the index in the `channelDescs` array passed to Host-side `MakeMultiChannelHandle`. |

## Return Value

Returns `BatchHandle<ChannelHandle>&` for preparing batched WQEs on the selected logical channel.

## Constraints

- `batchHandle` must be created from a valid `MultiChannelHandle`; do not pass a single-channel `UbcCtpBatchHandle`.
- `channelIndex` must be smaller than the `channelNum` used to create the `MultiChannelHandle`.
- Use the returned reference with batch read, write, or write-with-notify APIs.
- Pass the parent multi-channel batch handle, not this return value, to `BatchCommit` and `Drain`.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
