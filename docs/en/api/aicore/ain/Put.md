# Put

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:15:01.431Z pushedAt=2026-11-09T06:47:54.954Z nl=2 -->

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

Submits a one-sided write task by specifying the communication team and the peer rank ID, writing data in the local symmetric window to the peer symmetric window.

`Put` resolves the communication channel based on `team`, `peer` and the `contextIndex` of the `Ain` instance, resolves remote/local GM (Global Memory) addresses from `dstWin`, `srcWin` and offsets, and submits the communication task through the underlying Hcomm write API. `AinSignalInc` or `AinSignalAdd` can optionally be configured as a remote action, which appends a remote signal atomic add operation after the write task.

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
    AscendC::HcclCommSymWindow dstWin,
    uint64_t dstOffset,
    AscendC::HcclCommSymWindow srcWin,
    uint64_t srcOffset,
    uint64_t bytes,
    RemoteAction remoteAction = RemoteAction{},
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `RemoteAction` | Remote action type, supporting `AinRemoteNone`, `AinSignalInc`, and `AinSignalAdd`. See Table 3. |
| `DescriptorUbuf` | Descriptor type of the UBuf (Unified Buffer) temporary workspace, defaulting to `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` indicates that the WQE (Work Queue Element) is submitted immediately after being assembled; `AIN_COMMIT_DELAYED` indicates that only the WQE is assembled, and a subsequent `AIN_COMMIT_IMMED` task is required to trigger the submission. |
| `Config` | WQE control configuration of `COMM_PROTOCOL_UB_CTP`, defaulting to `URMA_DEFAULT_CFG` (relax order execution + CQE in-order reporting + fence enabled + CQE reporting required). A custom `Config` can adjust fields such as `cqe`. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Remote rank ID within the team. |
| `dstWin` | Input | Remote destination symmetric window handle. |
| `dstOffset` | Input | Byte offset within the destination symmetric window. |
| `srcWin` | Input | Local source symmetric window handle. |
| `srcOffset` | Input | Byte offset within the source symmetric window. |
| `bytes` | Input | Write length, in bytes. |
| `remoteAction` | Input | Remote action after the write task, defaulting to `AinRemoteNone`, which appends no remote action. |
| `ubuf` | Input | Description of the temporary workspace of the UBuf (Unified Buffer) used by the underlying Hcomm. |

The type `AinDescriptorUbuf` of `ubuf` contains three fields: `addr` (`__ubuf__ uint8_t*`, the start address of the UBuf workspace), `bytes` (`uint32_t`, the workspace size in bytes), and `eventId` (`uint32_t`, reserved for the event ID used to synchronize with the underlying communication engine; currently fixed to `0`).

**Table 3** Remote actions

| Remote Action | Description |
| --- | --- |
| `AinRemoteNone{}` | Appends no remote action. |
| `AinSignalInc{signalWindow, signalOffset}` | Performs an atomic increment by 1 on the remote signal after the write task. |
| `AinSignalAdd{signalWindow, signalOffset, value}` | Performs an atomic add of `value` on the remote signal after the write task. |

Here, `signalWindow` is the symmetric window handle where the destination signal resides, `signalOffset` is the byte offset of the signal within `signalWindow`, and `value` is the value for the atomic add.

## Return Value

None.

## Usage Constraints

- The host side shall create the team and register symmetric windows before the call.
- `team`, `dstWin` and `srcWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + bytes` must fall within the registered memory range of `dstWin` corresponding to `peer`. `srcOffset + bytes` must fall within the registered memory range of `srcWin` corresponding to the local rank.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B with a start address aligned to 32 bytes.
- When `AIN_COMMIT_DELAYED` is used, this task does not trigger submission immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When `AIN_COMMIT_DELAYED` is used, the consecutively accumulated task slots must not exceed the task submission capacity of the channel (that is, the underlying SQ (Send Queue) depth sqDepth). The accumulated tasks must be submitted through `AIN_COMMIT_IMMED` before the capacity is exhausted; otherwise, subsequent tasks fail due to SQ overflow. Without a remote action, this API occupies 1 task slot. When the `AinSignalInc` or `AinSignalAdd` remote action is configured, an additional signal atomic add task is appended internally (occupying 2 task slots), so a single call occupies 3 task slots in total, which must be accounted for in capacity planning.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls followed by a final `AIN_COMMIT_IMMED` call), only the final submission should generate a CQE (Completion Queue Entry). Set the `cqe` field in `Config` to 0 for intermediate tasks and to 1 for the final task.
- `Put` submits a non-blocking communication task. Call [Flush](./Flush.md) to wait for all peer channel tasks in the team to complete, or call [FlushAsync](./FlushAsync.md) to obtain the specified peer channel and then call [Wait](./Wait.md) to wait for completion.
- `AinSignalInc` and `AinSignalAdd` are implemented through underlying Hcomm atomic add. The signal address shall be prepared according to `uint64_t` access requirements.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported. For a single `Put` call, the `bytes` parameter must satisfy `0 < bytes ≤ 256 * 1024 * 1024`.

## Call Example

The following is an example of calling the **Put** API. For the complete sample, see [01_basic_ring](../../../../../examples/aicore/ain/01_basic_ring/README.md).

```cpp
// Data size of a single communication transfer
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf (Unified Buffer) workspace size: must be no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_put_kernel(
    GM_ADDR team, GM_ADDR dstWin, GM_ADDR srcWin, GM_ADDR signalWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];
    AscendC::AinDescriptorUbuf hcommUbuf{hcommLocal, HCOMM_BUF_SIZE, 0U};
    // Create the Ain object
    AscendC::Ain ain;
    // Remote rankId
    uint32_t peer = (rankId + 1) % rankNum;
    // One-sided write: write the local srcWin data to the peer dstWin, and atomically increment the remote signal by 1 after the write completes
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);
    // Wait for all peer channel communication tasks in the team to complete
    ain.Flush(team);
}
```
