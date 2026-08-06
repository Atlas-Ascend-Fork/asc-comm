# PutValue

## Function Description

Submit a point-to-point immediate-value write task via the specified communication team and peer rank, writing `value` to a peer symmetric window.

`PutValue` resolves the communication channel based on `team`, `peer` and the `contextIndex` of the `Ain` instance, resolves the remote GM address from `dstWin` and `dstOffset`, and submits the communication task through the underlying Hcomm immediate-value write API. `AinSignalInc` or `AinSignalAdd` can optionally be configured as a remote action, which appends a remote signal atomic add operation after the write task.

## Function Prototype

```cpp
template <
    typename T,
    typename RemoteAction = AscendC::AinRemoteNone,
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_INLINE_CFG>
__aicore__ inline void PutValue(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::HcommWindowHandle dstWin,
    uint64_t dstOffset,
    T value,
    RemoteAction remoteAction = RemoteAction{},
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `dstWin` | Input | Destination symmetric window handle of the peer. |
| `dstOffset` | Input | Byte offset in the destination symmetric window. |
| `value` | Input | Immediate value to write to the peer. |
| `remoteAction` | Input | Remote action after the write task. Default: `AinRemoteNone`, which does not trigger a remote signal operation. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Immediate value type. |
| `RemoteAction` | Remote action type. Supports `AinRemoteNone`, `AinSignalInc` and `AinSignalAdd`. |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means assembling the WQE and ringing the doorbell immediately. `AIN_COMMIT_DELAYED` means assembling only the WQE, and requires a subsequent `AIN_COMMIT_IMMED` task to be called to trigger submission. |
| `Config` | URMA WQE control configuration. Default: `URMA_INLINE_CFG`(strong ordering / FenceOn / CqeOn / Inline mode). |

## Remote Actions

| Type | Description |
| --- | --- |
| `AinRemoteNone` | Do not append a remote action. |
| `AinSignalInc` | After the write task, atomically add 1 to the remote signal corresponding to `remoteAction.signalWindow` and `remoteAction.signalOffset`. |
| `AinSignalAdd` | After the write task, atomically add `remoteAction.value` to the remote signal corresponding to `remoteAction.signalWindow` and `remoteAction.signalOffset`. |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- `team` and `dstWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + sizeof(T)` must fall within the registered memory range of `dstWin` corresponding to `peer`.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- `PutValue` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- `AinSignalInc` and `AinSignalAdd` are implemented through underlying Hcomm atomic add. The signal address shall be prepared according to `uint64_t` access requirements.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
