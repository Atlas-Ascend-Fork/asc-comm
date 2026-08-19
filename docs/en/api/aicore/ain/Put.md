# Put

## Function Description

Submit a one-sided write task via the specified communication team and peer rank, writing data from a local symmetric window to a peer symmetric window.

`Put` resolves the communication channel based on `team`, `peer` and the `contextIndex` of the `Ain` instance, resolves remote/local GM addresses from `dstWin`, `srcWin` and offsets, and submits the communication task through the underlying Hcomm write API. `AinSignalInc` or `AinSignalAdd` can optionally be configured as a remote action, which appends a remote signal atomic add operation after the write task.

## Function Prototype

```cpp
template <
    typename RemoteAction = AscendC::AinRemoteNone,
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_DEFAULT_CFG>
__aicore__ inline void Put(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::HcommWindowHandle dstWin,
    uint64_t dstOffset,
    AscendC::HcommWindowHandle srcWin,
    uint64_t srcOffset,
    uint64_t bytes,
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
| `srcWin` | Input | Source symmetric window handle of the local rank. |
| `srcOffset` | Input | Byte offset in the source symmetric window. |
| `bytes` | Input | Write length in bytes. |
| `remoteAction` | Input | Remote action after the write task. Default: `AinRemoteNone`, which does not trigger a remote signal operation. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `RemoteAction` | Remote action type. Supports `AinRemoteNone`, `AinSignalInc` and `AinSignalAdd`. |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means assembling the WQE and ringing the doorbell immediately. `AIN_COMMIT_DELAYED` means assembling only the WQE, and requires a subsequent `AIN_COMMIT_IMMED` task to be called to trigger submission. |
| `Config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG`(strong ordering / FenceOn / CqeOn). |

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
- `team`, `dstWin` and `srcWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + bytes` must fall within the registered memory range of `dstWin` corresponding to `peer`. `srcOffset + bytes` must fall within the registered memory range of `srcWin` corresponding to the local rank.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- `Put` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- `AinSignalInc` and `AinSignalAdd` are implemented through underlying Hcomm atomic add. The signal address shall be prepared according to `uint64_t` access requirements.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported. For a single `Put` call, the valid range of the input parameter `bytes` is `0 < bytes <= 256 * 1024 * 1024`.
