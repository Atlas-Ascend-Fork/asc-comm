# Signal

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:18:47.874Z pushedAt=2026-11-09T06:47:54.956Z nl=2 -->

## Supported Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference products: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training products: Not supported
<!-- end id6 -->

## Function Description

Header file path: `"ain/ain.h"`.

Performs an atomic add operation on the remote signal by specifying the communication team and the peer rank ID.

`Signal` resolves the communication channel based on `team`, `peer` and the `contextIndex` of the `Ain` instance, resolves the remote signal address from `action.signalWindow` and `action.signalOffset`, and submits an atomic add task through the underlying Hcomm.

## Function Prototype

```cpp
template <
    typename RemoteAction,
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_DEFAULT_CFG>
__aicore__ inline void Signal(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    RemoteAction action,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `RemoteAction` | Signal action type, supporting `AinSignalInc` and `AinSignalAdd`. See Table 3. |
| `DescriptorUbuf` | UBuf (Unified Buffer) temporary workspace descriptor type, defaulting to `AinDescriptorUbuf`. |
| `CommitFlags` | Task commit mode. `AIN_COMMIT_IMMED` means the commit is triggered immediately after the WQE (Work Queue Element) is assembled; `AIN_COMMIT_DELAYED` means only the WQE is assembled, and a subsequent `AIN_COMMIT_IMMED` task is required to trigger the commit in a unified manner. |
| `Config` | `COMM_PROTOCOL_UB_CTP` WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order execution + CQE (Completion Queue Element) in-order reporting + fence enabled + CQE reporting required). A custom `Config` can adjust fields such as `cqe`. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Remote rank ID within the team. |
| `action` | Input | Signal action, supporting `AinSignalInc` and `AinSignalAdd`. |
| `ubuf` | Input | Description of the temporary workspace of the UBuf (Unified Buffer) used by the underlying Hcomm. |

The `ubuf` type `AinDescriptorUbuf` contains three fields: `addr` (`__ubuf__ uint8_t*`, the start address of the UBuf workspace), `bytes` (`uint32_t`, the workspace size in bytes), and `eventId` (`uint32_t`, reserved for the event ID used to synchronize with the underlying communication engine; currently fixed to `0`).

**Table 3** Signal action description

| Signal Action | Description |
| --- | --- |
| `AinSignalInc{signalWindow, signalOffset}` | Performs an atomic add by 1 on the remote signal. |
| `AinSignalAdd{signalWindow, signalOffset, value}` | Performs an atomic add of `value` on the remote signal. |

Here, `signalWindow` is the symmetric window handle where the destination signal resides, `signalOffset` is the byte offset of the signal within `signalWindow`, and `value` is the value for the atomic add.

## Return Value

None.

## Usage Constraints

- The host side shall create the team and register the symmetric window containing the signal before the call.
- `team` and `action.signalWindow` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- The address corresponding to `action.signalOffset` must fall within the registered memory range of the symmetric window where the `peer` signal resides.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B with a start address aligned to 32 bytes.
- When `AIN_COMMIT_DELAYED` is used, this task does not trigger submission immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When `AIN_COMMIT_DELAYED` is used, the consecutively accumulated task slots must not exceed the task submission capacity of the channel (that is, the underlying SQ (Send Queue) depth sqDepth). Submit the accumulated tasks through `AIN_COMMIT_IMMED` before the capacity is exhausted; otherwise, subsequent tasks fail due to SQ overflow. This API occupies 2 task slots.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` submissions followed by a final `AIN_COMMIT_IMMED`), only the final submission should generate a CQE (Completion Queue Entry). Set the `cqe` field in the `Config` of intermediate tasks to 0 and that of the final task to 1. By default, the `cqe` of `URMA_DEFAULT_CFG`/`URMA_INLINE_CFG` is 1, so each task generates a CQE if the configuration is not adjusted.
- `Signal` submits a non-blocking communication task. Call [Flush](./Flush.md) to wait for all peer channel tasks in the team to complete, or call [FlushAsync](./FlushAsync.md) to obtain the specified peer channel and then call [Wait](./Wait.md) to wait for completion.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.

## Call Example

The following is an example of calling the **Signal** API.

```cpp
// Initialize the UBuf (Unified Buffer) workspace size: no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_signal_kernel(
    GM_ADDR team, GM_ADDR signalWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];
    AscendC::AinDescriptorUbuf hcommUbuf{hcommLocal, HCOMM_BUF_SIZE, 0U};
    // Create the Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // Value for the signal atomic add.
    uint64_t value = 1U;

    // Perform an atomic add operation on the remote signal, adding 1 per call.
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);

    // Perform an atomic add of value on the remote signal.
    ain.Signal<AscendC::AinSignalAdd>(team, peer,
        AscendC::AinSignalAdd{signalWin, 0U, value}, hcommUbuf);

    // Wait for all peer channel communication tasks in the team to complete.
    ain.Flush(team);
}
```
