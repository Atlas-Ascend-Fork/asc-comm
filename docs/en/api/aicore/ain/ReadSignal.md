# ReadSignal

## Function Description

Read the local signal value of the current rank, and return the value masked to the low-order width specified by `bits`.

`ReadSignal` resolves the signal address corresponding to the current rank based on `team`, `signalWindow` and `signalOffset`, and reads the `uint64_t` signal value through a device-side read operation.

## Function Prototype

```cpp
__aicore__ inline uint64_t ReadSignal(
    AscendC::HcommTeamHandle team,
    AscendC::HcommWindowHandle signalWindow,
    size_t signalOffset,
    uint32_t bits = 64,
    AscendC::AinMemoryOrder order = AscendC::AIN_MEMORY_ORDER_RELAX) const;
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `signalWindow` | Input | Symmetric window handle containing the signal. |
| `signalOffset` | Input | Byte offset of the signal in the symmetric window. |
| `bits` | Input | Number of low-order bits to read. Valid range: 1-64. Default: 64. |
| `order` | Input | Memory order for signal read. Default: `AIN_MEMORY_ORDER_RELAX`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `uint64_t` | Local signal value of the current rank after masking to the low `bits` bits. |

## Usage Constraints

- The Host side shall create the team and register the symmetric window containing the signal before invocation.
- `team` and `signalWindow` must be valid device-side handles.
- The address corresponding to `signalOffset` must fall within the registered memory range of the signal window corresponding to the local rank.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `bits` is used to generate the low-order mask. Valid range: 1-64. When `bits` is 64, the complete 64-bit signal value is read.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee memory ordering for ordinary data accesses before and after signal operations.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
