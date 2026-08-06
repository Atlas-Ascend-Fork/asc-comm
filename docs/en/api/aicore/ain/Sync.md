# Sync

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

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `order` | Input | Memory order for barrier signal wait. Currently supports `AIN_MEMORY_ORDER_RELAX`. |
| `timeoutCycles` | Input | Maximum number of polling attempts. Used only by the timeout overload. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |

## Return Value

The no-timeout overload has no return value.

The timeout overload returns:

| Return Value | Description |
| --- | --- |
| `0` | Synchronization succeeded. |
| `-1` | Waiting exceeded `timeoutCycles`; synchronization failed. |

## Usage Constraints

- A valid `AinBarrierSession` must be constructed before invocation.
- The Host side shall create the team and prepare the synchronization memory used by barrier before invocation.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee memory ordering for ordinary data accesses before and after signal operations.
- The no-timeout overload keeps polling until all other ranks arrive at the barrier. The caller must ensure that ranks in the team call it symmetrically to avoid permanent Kernel waiting.
- For the timeout overload, `timeoutCycles` indicates the number of polling attempts. Returning `-1` only indicates that the current rank timed out while waiting, and does not roll back signals that have already been sent.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
