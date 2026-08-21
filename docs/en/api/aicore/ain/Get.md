# Get

## Function Description

Submit a one-sided read task via the specified communication team and peer rank, reading data from a peer symmetric window to a local symmetric window.

`Get` resolves the communication channel based on `team`, `peer` and the `contextIndex` of the `Ain` instance, resolves local/remote GM addresses from `dstWin`, `srcWin` and offsets, and submits the communication task through the underlying Hcomm read API.

## Function Prototype

```cpp
template <
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_DEFAULT_CFG>
__aicore__ inline void Get(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::HcommWindowHandle dstWin,
    uint64_t dstOffset,
    AscendC::HcommWindowHandle srcWin,
    uint64_t srcOffset,
    uint64_t bytes,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `dstWin` | Input | Destination symmetric window handle of the local rank. |
| `dstOffset` | Input | Byte offset in the destination symmetric window. |
| `srcWin` | Input | Source symmetric window handle of the peer. |
| `srcOffset` | Input | Byte offset in the source symmetric window. |
| `bytes` | Input | Read length in bytes. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means assembling the WQE and ringing the doorbell immediately. `AIN_COMMIT_DELAYED` means assembling only the WQE, and requires a subsequent `AIN_COMMIT_IMMED` task to be called to trigger submission. |
| `Config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG`(strong ordering / FenceOn / CqeOn). |

## Return Value

No return value.

## Usage Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- `team`, `dstWin` and `srcWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + bytes` must fall within the registered memory range of `dstWin` corresponding to the local rank. `srcOffset + bytes` must fall within the registered memory range of `srcWin` corresponding to `peer`.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- `Get` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported. For a single `Get` call, the valid range of the input parameter `bytes` is `0 < bytes <= 256 * 1024 * 1024`.
