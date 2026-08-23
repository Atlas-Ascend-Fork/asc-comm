# GetHandleRef

## Function Description

Selects one logical channel and remote registered-memory region from a shared-Jetty multi-channel batch handle and returns the BatchHandle reference used by batch read, write, write-with-notify, commit, and drain APIs.

## Function Prototype

```cpp
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    BatchHandle<MultiChannelHandle>& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Multi-channel batch handle created by `MakeBatchHandle(MultiChannelHandle, ...)`. |
| `channelIndex` | Input | Logical channel index. It matches the index in the `channelDescs` array passed to Host-side `MakeMultiChannelHandle`. |
| `remoteAddr` | Input | Address in a remote registered buffer of the selected logical channel. |

## Return Value

Returns the inner `BatchHandle<ChannelHandle>&` configured for the selected logical channel and remote MR.

## Constraints

- `batchHandle` must be created from a valid `MultiChannelHandle`; do not pass a single-channel `UbcCtpBatchHandle`.
- `channelIndex` must be smaller than the `channelNum` used to create the `MultiChannelHandle`.
- `remoteAddr` must not be null and must belong to a remote registered buffer of the selected logical channel.
- Repeated calls on the same multi-channel batch handle return the same inner BatchHandle reference. Each call updates the logical-channel and remote MR information currently selected in that inner handle.
- Batch operations through the returned reference must access the selected MR. Call this interface again before selecting another MR.
- Use the returned inner reference with batch read, write, write-with-notify, `BatchCommit`, and batch `Drain` APIs.
- The multi-channel batch handle is only for selecting logical channels through this interface. Do not pass it directly to the execution APIs above.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
