# GetHandleRef

## Function Description

Selects one logical channel and remote registered-memory region from a shared-Jetty multi-channel batch handle and returns the BatchHandle reference used by batch read, write, write-with-notify, commit, and drain APIs.

## Function Prototype

```cpp
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    BatchHandle<MultiChannelHandle>& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr = nullptr);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Multi-channel batch handle created by `MakeBatchHandle(MultiChannelHandle, ...)`. |
| `channelIndex` | Input | Logical channel index. It matches the index in the `channelDescs` array passed to Host-side `MakeMultiChannelHandle`. |
| `remoteAddr` | Input | Address in a remote registered buffer of the selected logical channel. When null, the first remote registered buffer is selected. Default: `nullptr`. |

## Return Value

Returns the inner `BatchHandle<ChannelHandle>&` configured for the selected logical channel and remote MR.

## Constraints

- `batchHandle` must be created from a valid `MultiChannelHandle`; do not pass a single-channel `UbcCtpBatchHandle`.
- `channelIndex` must be smaller than the `channelNum` used to create the `MultiChannelHandle`.
- A non-null `remoteAddr` must belong to a remote registered buffer of the selected logical channel. When null, the channel must contain at least one remote registered buffer.
- Repeated calls on the same multi-channel batch handle return the same inner BatchHandle reference. Each call updates the logical-channel and remote MR information currently selected in that inner handle.
- Remote buffers accessed through the returned reference must use the token cached by this call. Call this interface again before accessing a remote buffer that uses a different token.
- Use the returned inner reference with batch read, write, write-with-notify, `BatchCommit`, and batch `Drain` APIs.
- The multi-channel batch handle is only for selecting logical channels through this interface. Do not pass it directly to the execution APIs above.

## Related APIs

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
