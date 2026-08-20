# signal

## Function Description

Perform an atomic add operation on a remote signal via the specified communication team and peer rank.

`signal` resolves the communication channel based on `team`, `peer` and the `context_index` of the `ain` instance, resolves the remote signal address from `action.signal_window` and `action.signal_offset`, and submits an atomic add task through the underlying asccomm.

## Function Prototype

```cpp
template <
    typename remote_action_t,
    typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf,
    AscendC::ain_commit_flags commit_flags = AscendC::ain_commit_immed,
    auto const& config = urma_default_cfg>
__aicore__ inline void signal(
    AscendC::asccomm_team_handle team,
    uint32_t peer,
    remote_action_t action,
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `action` | Input | signal action. Supports `ain_signal_inc` and `ain_signal_add`. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying asccomm. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `remote_action_t` | signal action type. Supports `ain_signal_inc` and `ain_signal_add`. |
| `descriptor_ubuf_t` | UBuf temporary workspace descriptor type. Default: `ain_descriptor_ubuf`. |
| `commit_flags` | Task submission mode. `ain_commit_immed` means assembling the WQE and ringing the doorbell immediately. `ain_commit_delayed` means assembling only the WQE, and requires a subsequent `ain_commit_immed` task to be called to trigger submission. |
| `config` | URMA WQE control configuration. Default: `urma_default_cfg`(strong ordering / FenceOn / CqeOn). |

## signal Actions

| Type | Description |
| --- | --- |
| `ain_signal_inc` | Atomically add 1 to the remote signal corresponding to `action.signal_window` and `action.signal_offset`. |
| `ain_signal_add` | Atomically add `action.value` to the remote signal corresponding to `action.signal_window` and `action.signal_offset`. |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team and register the symmetric window containing the signal before invocation.
- `team` and `action.signal_window` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- The address corresponding to `action.signal_offset` must fall within the registered memory range of the signal window corresponding to `peer`.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying asccomm initialization. `descriptor_ubuf_t` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `ain_commit_delayed` is used, this task does not ring the doorbell immediately. At least one subsequent `ain_commit_immed` task must be submitted to ensure that previous tasks are submitted.
- `signal` submits a non-blocking communication task. Call `flush` to wait for tasks on all peer channels in the team to complete, or call `flush_async` to get a specified peer channel and then call `wait`.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported.
