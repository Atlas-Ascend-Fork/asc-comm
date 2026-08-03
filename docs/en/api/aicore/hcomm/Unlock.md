# Unlock

## Function Description

Releases a communication channel lock acquired by [Lock](./Lock.md).

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
| `-1` | The lock could not be released. |

## Constraints

- This API is available only to AIV on Ascend 950 over the `COMM_PROTOCOL_UBC_CTP` path. The `COMM_PROTOCOL_ROCE` path is not supported.
- Call `Unlock` only after [Lock](./Lock.md) succeeds, and release the lock on the same AI Core that acquired it.
- Do not call `Unlock` repeatedly for the same lock acquisition.
- Use this interface only to synchronize AI Cores sharing the same channel on the same device.
