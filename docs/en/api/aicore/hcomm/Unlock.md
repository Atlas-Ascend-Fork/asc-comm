# Unlock

## Function Description

Publishes the state of a communication channel and releases its cross-AI-Core lock. This API is available only to AIV on Ascend 950 over the `COMM_PROTOCOL_UBC_CTP` path.

Before releasing the lock, the API writes back and invalidates the DataCache line containing the channel counters and executes a DDR data synchronization barrier. This ensures that protected channel state becomes visible to the next lock owner before the lock is released.

## Function Prototype

```cpp
__aicore__ inline int32_t Unlock(AscendC::ChannelHandle channel);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The lock was released successfully. |
| `-1` | The lock could not be released because `channel` is null, does not meet the channel entity alignment requirement, or the lock was not HELD before release. |

## Constraints

- This API is available only to AIV on Ascend 950 over the `COMM_PROTOCOL_UBC_CTP` path. The `COMM_PROTOCOL_ROCE` path is not supported.
- `Unlock` may be called only after the current AI Core has successfully called [Lock](./Lock.md), and the lock must be released by the same AI Core that acquired it.
- The implementation does not record or validate the lock owner. Calling `Unlock` from an AI Core that did not acquire the lock may release a lock held by another AI Core and break mutual exclusion. The caller must maintain the correct pairing.
- `Unlock` must not be called repeatedly. The API returns `-1` when the lock is not HELD.
- A BatchHandle caches queue cursors when it is created. Locking individual batch operations does not make concurrent BatchHandles or mixing ordinary and batch interfaces safe; the BatchHandle must still exclusively own its channel or shared Jetty for its entire lifetime.
- Cache write-through covers only the cache line containing the channel's `sqHead`, `sqTail`, `cqHead` and `cqTail`. It does not flush arbitrary user GM addresses and cannot be used as a general-purpose publication operation for GM data.
- The lock synchronizes AI Cores sharing the same channel on the same device. It does not provide cross-rank or cross-device synchronization.
