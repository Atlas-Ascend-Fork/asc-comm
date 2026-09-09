# Unlock

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

Flushes channel state and releases the cross-AI-Core lock of a communication channel acquired through [Lock](./Lock.md). Before releasing, the queue counters of the channel (`sqHead`, `sqTail`, `cqHead`, `cqTail`) are written through to GM to ensure visibility to other AI Cores.

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
| `0` | The lock is released successfully. |
| `-1` | `channel` is null or the address is misaligned, or the lock is not held (for example, the lock was never acquired or is released repeatedly). |

## Constraints

- Supported only by AIV on the Ascend 950 platform over the `COMM_PROTOCOL_UBC_CTP` path. The `COMM_PROTOCOL_ROCE` path is not supported.
- Call `Unlock` only after [Lock](./Lock.md) succeeds, and release the lock on the same AI Core that acquired it.
- Call `Unlock` after the last operation that updates channel state, so that the flushed channel state is visible to other AI Cores.
- Do not call `Unlock` repeatedly for the same lock acquisition.
- Use this interface only to synchronize AI Cores that share the same channel on the same device.

## Example

The calling example of Unlock is as follows:

```cpp
hcomm.Lock(channel);
// Operations that update channel state
hcomm.Unlock(channel);    // Flush channel state and release the lock
```
