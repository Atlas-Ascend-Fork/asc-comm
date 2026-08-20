# sync

## Function Description

Perform barrier synchronization within the team bound to `ain_barrier_session`.

`sync` first sends barrier signals to other ranks in the team, and then waits for other ranks to write the barrier signal of the current rank. The no-timeout overload waits until synchronization completes. The timeout overload returns failure when the number of polling attempts exceeds `timeout_cycles`.

## Function Prototype

```cpp
template <typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf>
__aicore__ inline void sync(
    AscendC::ain_memory_order order,
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

```cpp
template <typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf>
__aicore__ inline int32_t sync(
    AscendC::ain_memory_order order,
    uint64_t timeout_cycles,
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `order` | Input | Memory order for barrier signal wait. Currently supports `ain_memory_order_relax`. |
| `timeout_cycles` | Input | Maximum number of polling attempts. Used only by the timeout overload. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying asccomm. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `descriptor_ubuf_t` | UBuf temporary workspace descriptor type. Default: `ain_descriptor_ubuf`. |

## Return Value

The no-timeout overload has no return value.

The timeout overload returns:

| Return Value | Description |
| --- | --- |
| `0` | Synchronization succeeded. |
| `-1` | Waiting exceeded `timeout_cycles`; synchronization failed. |

## Usage Constraints

- A valid `ain_barrier_session` must be constructed before invocation.
- The Host side shall create the team and prepare the synchronization memory used by barrier before invocation.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying asccomm initialization. `descriptor_ubuf_t` currently must provide a UBuf temporary workspace no smaller than 512 B.
- Currently, only `ain_memory_order_relax` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee memory ordering for ordinary data accesses before and after signal operations.
- The no-timeout overload keeps polling until all other ranks arrive at the barrier. The caller must ensure that ranks in the team call it symmetrically to avoid permanent Kernel waiting.
- For the timeout overload, `timeout_cycles` indicates the number of polling attempts. Returning `-1` only indicates that the current rank timed out while waiting, and does not roll back signals that have already been sent.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported.
