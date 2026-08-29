# Sync

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol
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

Perform barrier synchronization within the team bound to `AinBarrierSession`.

`Sync` first sends barrier signals to other ranks in the team, and then waits for other ranks to write the barrier signal of the current rank. The no-timeout overload waits until synchronization completes. The timeout overload returns failure when the number of polling attempts exceeds `timeoutCycles`.

## Function Prototype

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline void Sync(
    AscendC::AinMemoryOrder order,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline int32_t Sync(
    AscendC::AinMemoryOrder order,
    uint64_t timeoutCycles,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `order` | Input | Memory order for barrier signal wait. Currently supports `AIN_MEMORY_ORDER_RELAX`. |
| `timeoutCycles` | Input | Maximum number of polling attempts. Used only by the timeout overload. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Return Value

The no-timeout overload has no return value.

The timeout overload returns:

| Return Value | Description |
| --- | --- |
| `0` | Synchronization succeeded. |
| `-1` | Waiting exceeded `timeoutCycles`; synchronization failed. |

## Constraints

- A valid `AinBarrierSession` must be constructed before invocation.
- The Host side shall create the team and prepare the synchronization memory used by barrier before invocation.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee memory ordering for ordinary data accesses before and after signal operations.
- The no-timeout overload keeps polling until all other ranks arrive at the barrier. The caller must ensure that ranks in the team call it symmetrically to avoid permanent Kernel waiting.
- For the timeout overload, `timeoutCycles` indicates the number of polling attempts. Returning `-1` only indicates that the current rank timed out while waiting, and does not roll back signals that have already been sent.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.

## Example

The calling example of Sync is as follows:

```cpp
AscendC::Ain<> ain;
AscendC::AinBarrierSession<> barrierSession(&ain, team, 0);

// Synchronization without timeout
barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX);

// Synchronization with timeout
int32_t ret = barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 10000);
if (ret != 0) {
    // Timeout handling
}
```
