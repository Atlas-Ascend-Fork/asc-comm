# FlushAsync

## Function Description

Get the communication channel handle corresponding to a specified peer for subsequent `Wait` calls to wait for communication tasks on that channel to complete.

`FlushAsync` itself does not block waiting for communication task completion. It only resolves the channel handle based on `team`, `peer` and the `contextIndex` of the `Ain` instance, and writes it to `outChannelHandle`.

## Function Prototype

```cpp
__aicore__ inline void FlushAsync(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::ChannelHandle* outChannelHandle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `outChannelHandle` | Output | Returned communication channel handle. |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team before invocation.
- `team` must be a valid device-side handle. `peer` must be a valid rank ID in the team.
- `outChannelHandle` must point to a valid writable address.
- `FlushAsync` does not wait for task completion. The caller needs to call `Wait` to wait for tasks on the channel corresponding to `outChannelHandle` to complete.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
