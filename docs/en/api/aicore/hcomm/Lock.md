# Lock

## Function Description

Acquires the cross-AI-Core lock of a communication channel to protect channel state when multiple AI Cores share the same channel. This API is available only to AIV on Ascend 950 over the `COMM_PROTOCOL_UBC_CTP` path.

If another AI Core holds the lock, this API keeps spinning until the lock is acquired.

## Function Prototype

```cpp
__aicore__ inline int32_t Lock(AscendC::ChannelHandle channel);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The lock was acquired successfully. |
| `-1` | The lock could not be acquired because `channel` is null or does not meet the channel entity alignment requirement. |

## Constraints

- This API is available only to AIV on Ascend 950 over the `COMM_PROTOCOL_UBC_CTP` path. The `COMM_PROTOCOL_ROCE` path is not supported.
- `channel` must point to a correctly initialized communication channel whose lock field is initially FREE.
- The API has no timeout and does not guarantee acquisition order among AI Cores. The caller keeps spinning if another AI Core holds the lock indefinitely.
- The lock is not reentrant. After a successful `Lock`, the same AI Core must not call `Lock` again for the same channel.
- Every successful `Lock` must be paired with [Unlock](./Unlock.md) on the same AI Core. All exit paths after lock acquisition must release the lock.
- When multiple AI Cores share a channel, all accesses that update channel state must be placed between `Lock` and `Unlock`.
- A BatchHandle caches queue cursors when it is created. Locking individual batch operations does not make concurrent BatchHandles or mixing ordinary and batch interfaces safe; the BatchHandle must still exclusively own its channel or shared Jetty for its entire lifetime.
- The lock synchronizes AI Cores sharing the same channel on the same device. It does not provide cross-rank or cross-device synchronization.
- After acquiring the lock, the API executes a DDR data synchronization barrier so that channel state published by the previous owner through `Unlock` is visible to the current AI Core.
