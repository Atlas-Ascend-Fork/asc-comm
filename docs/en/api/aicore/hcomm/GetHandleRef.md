# GetHandleRef

## Function Description

Selects one logical channel from a shared-Jetty multi-channel batch handle and returns the BatchHandle reference used
by batch read, write, write-with-notify, commit, and drain APIs.

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

Returns `BatchHandle<ChannelHandle>&`. The reference points to the only execution handle inside the multi-channel
batch handle. Use it to prepare batched WQEs on the selected logical channel and to call `BatchCommit` and batch
`Drain`.

## Constraints

- `batchHandle` must be created from a valid `MultiChannelHandle`; do not pass a single-channel `UbcCtpBatchHandle`.
- `channelIndex` must be smaller than the `channelNum` used to create the `MultiChannelHandle`.
- Repeated calls on the same multi-channel batch handle return the same inner BatchHandle reference. Each call updates
  the logical-channel information currently selected in that inner handle.
- Use the returned inner reference with batch read, write, write-with-notify, `BatchCommit`, and batch `Drain` APIs.
- The multi-channel batch handle is only for selecting logical channels through this interface. Do not pass it
  directly to the execution APIs above.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
