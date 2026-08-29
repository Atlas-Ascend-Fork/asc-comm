# Lock

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol but not the RoCE protocol
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Not supported
<!-- end id7 -->

## Function Description

Acquires the cross-AI-Core lock of a communication channel when multiple AI Cores share the same channel. When `channel` is valid and the lock is already held, the call waits until the lock is successfully acquired and does not return failure due to lock contention. The order in which multiple AI Cores acquire the lock is not guaranteed.

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
| `0` | The lock is acquired successfully. |
| `-1` | `channel` is null or the address is misaligned. Lock contention does not return `-1`. |

## Constraints

- Supported only by AIV on the Ascend 950 platform over the `COMM_PROTOCOL_UBC_CTP` path. The `COMM_PROTOCOL_ROCE` path is not supported.
- The lock is not reentrant. Do not call `Lock` again on the same channel before the lock is released.
- All accesses that update channel state must be protected by `Lock` and [Unlock](./Unlock.md).
- After each successful `Lock` call, `Unlock` must be called by the same AI Core, and the lock must also be released on error exit paths.
- Use this interface only to synchronize AI Cores that share the same channel on the same device.

## Example

The calling example of Lock is as follows:

```cpp
hcomm.Lock(channel);
// Operations that update channel state
hcomm.Unlock(channel);    // Release the lock
```
