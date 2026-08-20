# flush

## Function Description

wait for communication tasks on all peer channels in the specified communication team to complete.

`flush` traverses all ranks in the team except the current rank, resolves the corresponding communication channel based on the `context_index` of the `ain` instance, and invokes the underlying asccomm completion-wait API on each channel.

## Function Prototype

```cpp
__aicore__ inline void flush(AscendC::asccomm_team_handle team);
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
- `flush` waits for submitted tasks on all peer channels in the team to complete. To wait for a single peer channel, call `flush_async` to obtain the channel handle and then call `wait`.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported.
