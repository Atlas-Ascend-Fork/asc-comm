# WaitSignal

## Function Description

Block and wait until the local signal value of the current rank reaches the specified threshold.

`WaitSignal` resolves the signal address corresponding to the current rank based on `team`, `signalWindow` and `signalOffset`, and repeatedly reads the signal value masked to the low `bits` bits until the value is greater than or equal to `least`.

## Function Prototype

```cpp
__aicore__ inline void WaitSignal(
    AscendC::HcommTeamHandle team,
    AscendC::HcommWindowHandle signalWindow,
    size_t signalOffset,
    uint64_t least,
    uint32_t bits = 64,
    AscendC::AinMemoryOrder order = AscendC::AIN_MEMORY_ORDER_RELAX) const;
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `signalWindow` | Input | Symmetric window handle containing the signal. |
| `signalOffset` | Input | Byte offset of the signal in the symmetric window. |
| `least` | Input | Wait threshold. |
| `bits` | Input | Number of low-order bits to compare. Valid range: 1-64. Default: 64. |
| `order` | Input | Memory order for signal wait. Default: `AIN_MEMORY_ORDER_RELAX`. |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team and register the symmetric window containing the signal before invocation.
- `team` and `signalWindow` must be valid device-side handles.
- The address corresponding to `signalOffset` must fall within the registered memory range of the signal window corresponding to the local rank.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `bits` is used to generate the low-order mask. Valid range: 1-64. When `bits` is 64, the complete 64-bit signal value is compared.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee memory ordering for ordinary data accesses before and after signal operations.
- `WaitSignal` keeps polling until the condition is satisfied. The caller must ensure that the remote side will update this signal to avoid permanent Kernel waiting.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
