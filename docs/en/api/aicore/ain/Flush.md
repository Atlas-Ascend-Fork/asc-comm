# Flush

## Function Description

Wait for communication tasks on all peer channels in the specified communication team to complete.

`Flush` traverses all ranks in the team except the current rank, resolves the corresponding communication channel based on the `contextIndex` of the `Ain` instance, and invokes the underlying Hcomm completion-wait API on each channel.

## Function Prototype

```cpp
__aicore__ inline void Flush(AscendC::HcommTeamHandle team);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team before invocation.
- `team` must be a valid device-side handle.
- `Flush` waits for submitted tasks on all peer channels in the team to complete. To wait for a single peer channel, call `FlushAsync` to obtain the channel handle and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
