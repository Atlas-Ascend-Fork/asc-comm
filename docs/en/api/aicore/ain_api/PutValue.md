# PutValue

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Not supported
<!-- end id7 -->

## Function Description

The header file path is `"comm_api/aicore/ain/ain.h"`.

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
    AscendC::HcclCommSymWindow dstWin,
    uint64_t dstOffset,
    T value,
    RemoteAction remoteAction = RemoteAction{},
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Immediate value type. |
| `RemoteAction` | Remote action type. Supports `AinRemoteNone`, `AinSignalInc` and `AinSignalAdd`. |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means assembling the WQE and ringing the doorbell immediately. `AIN_COMMIT_DELAYED` means assembling only the WQE, and requires a subsequent `AIN_COMMIT_IMMED` task to be called to trigger submission. |
| `Config` | URMA WQE control configuration. Default: `URMA_INLINE_CFG`(Relax Order(RO) + in order CQE + FenceOn + CqeOn + Inline mode). |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `dstWin` | Input | Destination symmetric window handle of the peer. |
| `dstOffset` | Input | Byte offset in the destination symmetric window. |
| `value` | Input | Immediate value to write to the peer. |
| `remoteAction` | Input | Remote action after the write task. Default: `AinRemoteNone`, which does not trigger a remote signal operation. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Remote Actions

| Type | Description |
| --- | --- |
| `AinRemoteNone` | Do not append a remote action. |
| `AinSignalInc` | After the write task, atomically add 1 to the remote signal corresponding to `remoteAction.signalWindow` and `remoteAction.signalOffset`. |
| `AinSignalAdd` | After the write task, atomically add `remoteAction.value` to the remote signal corresponding to `remoteAction.signalWindow` and `remoteAction.signalOffset`. |

## Return Value

No return value.

## Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- `team` and `dstWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + sizeof(T)` must fall within the registered memory range of `dstWin` corresponding to `peer`.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When `AIN_COMMIT_DELAYED` is used, the number of consecutive calls must not exceed the underlying SQ depth (`sqDepth`). Accumulated tasks must be submitted via `AIN_COMMIT_IMMED` before the SQ is exhausted; otherwise, subsequent tasks will fail due to SQ overflow.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls plus a final `AIN_COMMIT_IMMED`), only the last submission should produce a CQE.
- `PutValue` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- `AinSignalInc` and `AinSignalAdd` are implemented through underlying Hcomm atomic add. The signal address shall be prepared according to `uint64_t` access requirements.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.

## Example

The calling example of PutValue is as follows:

```cpp
// Size of data transferred in a single communication
constexpr uint64_t BYTES = sizeof(uint32_t);

extern "C" __global__ __aicore__ void ain_kernel(
      __gm__ void* team, __gm__ void* dstWin, __gm__ void* signalWin,
      uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        AscendC::HCOMM_URMA_TMP_BUF_SIZE, 0U};
    // Create an Ain object
    AscendC::Ain ain;
    // Peer rank ID
    uint32_t peer = (rankId + 1) % rankNum;
    // Immediate value to be written to the peer
    uint32_t value = 42;
    // Immediate write: write value to the peer dstWin, and atomically add 1 to the remote signal after the write is complete
    ain.PutValue(team, peer, dstWin, rankId * BYTES, value,
                 AscendC::AinSignalInc{signalWin, 0}, hcommUbuf);
    // Wait for communication tasks on all peer channels in the team to complete
    ain.Flush(team);
}
```
