# Lock

## Function Description

Acquires the lock of a communication channel when multiple AI Cores share the same channel. With a valid `channel`, if the lock is already held, the call waits until the lock is acquired and does not fail due to contention.

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
| `-1` | `channel` is null or misaligned. Lock contention does not return `-1`. |

## Constraints

- This API is available only to AIV on Ascend 950 over the `COMM_PROTOCOL_UBC_CTP` path. The `COMM_PROTOCOL_ROCE` path is not supported.
- The lock is not reentrant. Do not call `Lock` again for the same channel before releasing it.
- Every successful `Lock` must be paired with [Unlock](./Unlock.md) on the same AI Core, including on error paths.
- Use this interface only to synchronize AI Cores sharing the same channel on the same device.
