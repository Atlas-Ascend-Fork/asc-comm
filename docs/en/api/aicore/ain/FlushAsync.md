# flush_async

## Function Description

get the communication channel handle corresponding to a specified peer for subsequent `wait` calls to wait for communication tasks on that channel to complete.

`flush_async` itself does not block waiting for communication task completion. It only resolves the channel handle based on `team`, `peer` and the `context_index` of the `ain` instance, and writes it to `out_channel_handle`.

## Function Prototype

```cpp
__aicore__ inline void flush_async(
    AscendC::asccomm_team_handle team,
    uint32_t peer,
    AscendC::channel_handle* out_channel_handle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `out_channel_handle` | Output | Returned communication channel handle. |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team before invocation.
- `team` must be a valid device-side handle. `peer` must be a valid rank ID in the team.
- `out_channel_handle` must point to a valid writable address.
- `flush_async` does not wait for task completion. The caller needs to call `wait` to wait for tasks on the channel corresponding to `out_channel_handle` to complete.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported.
