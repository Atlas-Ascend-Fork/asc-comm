# put

## Function Description

Submit a one-sided write task via the specified communication team and peer rank, writing data from a local symmetric window to a peer symmetric window.

`put` resolves the communication channel based on `team`, `peer` and the `context_index` of the `ain` instance, resolves remote/local GM addresses from `dst_win`, `src_win` and offsets, and submits the communication task through the underlying asccomm write API. `ain_signal_inc` or `ain_signal_add` can optionally be configured as a remote action, which appends a remote signal atomic add operation after the write task.

## Function Prototype

```cpp
template <
    typename remote_action_t = AscendC::ain_remote_none,
    typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf,
    AscendC::ain_commit_flags commit_flags = AscendC::ain_commit_immed,
    auto const& config = urma_default_cfg>
__aicore__ inline void put(
    AscendC::asccomm_team_handle team,
    uint32_t peer,
    AscendC::asccomm_window_handle dst_win,
    uint64_t dst_offset,
    AscendC::asccomm_window_handle src_win,
    uint64_t src_offset,
    uint64_t bytes,
    remote_action_t remote_action = remote_action_t{},
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `dst_win` | Input | Destination symmetric window handle of the peer. |
| `dst_offset` | Input | Byte offset in the destination symmetric window. |
| `src_win` | Input | Source symmetric window handle of the local rank. |
| `src_offset` | Input | Byte offset in the source symmetric window. |
| `bytes` | Input | Write length in bytes. |
| `remote_action` | Input | Remote action after the write task. Default: `ain_remote_none`, which does not trigger a remote signal operation. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying asccomm. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `remote_action_t` | Remote action type. Supports `ain_remote_none`, `ain_signal_inc` and `ain_signal_add`. |
| `descriptor_ubuf_t` | UBuf temporary workspace descriptor type. Default: `ain_descriptor_ubuf`. |
| `commit_flags` | Task submission mode. `ain_commit_immed` means assembling the WQE and ringing the doorbell immediately. `ain_commit_delayed` means assembling only the WQE, and requires a subsequent `ain_commit_immed` task to be called to trigger submission. |
| `config` | URMA WQE control configuration. Default: `urma_default_cfg`(strong ordering / FenceOn / CqeOn). |

## Remote Actions

| Type | Description |
| --- | --- |
| `ain_remote_none` | Do not append a remote action. |
| `ain_signal_inc` | After the write task, atomically add 1 to the remote signal corresponding to `remote_action.signal_window` and `remote_action.signal_offset`. |
| `ain_signal_add` | After the write task, atomically add `remote_action.value` to the remote signal corresponding to `remote_action.signal_window` and `remote_action.signal_offset`. |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- `team`, `dst_win` and `src_win` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dst_offset + bytes` must fall within the registered memory range of `dst_win` corresponding to `peer`. `src_offset + bytes` must fall within the registered memory range of `src_win` corresponding to the local rank.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying asccomm initialization. `descriptor_ubuf_t` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `ain_commit_delayed` is used, this task does not ring the doorbell immediately. At least one subsequent `ain_commit_immed` task must be submitted to ensure that previous tasks are submitted.
- `put` submits a non-blocking communication task. Call `flush` to wait for tasks on all peer channels in the team to complete, or call `flush_async` to get a specified peer channel and then call `wait`.
- `ain_signal_inc` and `ain_signal_add` are implemented through underlying asccomm atomic add. The signal address shall be prepared according to `uint64_t` access requirements.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported. For a single `put` call, the valid range of the input parameter `bytes` is `0 < bytes <= 256 * 1024 * 1024`.
