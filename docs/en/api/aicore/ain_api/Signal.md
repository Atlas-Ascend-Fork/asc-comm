# Signal

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

Perform an atomic add operation on a remote signal via the specified communication team and peer rank.

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

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `RemoteAction` | Signal action type. Supports `AinSignalInc` and `AinSignalAdd`. |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means assembling the WQE and ringing the doorbell immediately. `AIN_COMMIT_DELAYED` means assembling only the WQE, and requires a subsequent `AIN_COMMIT_IMMED` task to be called to trigger submission. |
| `Config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG`(Relax Order(RO) + in order CQE + FenceOn + CqeOn). |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `action` | Input | Signal action. Supports `AinSignalInc` and `AinSignalAdd`. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Signal Actions

| Type | Description |
| --- | --- |
| `AinSignalInc` | Atomically add 1 to the remote signal corresponding to `action.signalWindow` and `action.signalOffset`. |
| `AinSignalAdd` | Atomically add `action.value` to the remote signal corresponding to `action.signalWindow` and `action.signalOffset`. |

## Return Value

No return value.

## Constraints

- The Host side shall create the team and register the symmetric window containing the signal before invocation.
- `team` and `action.signalWindow` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- The address corresponding to `action.signalOffset` must fall within the registered memory range of the signal window corresponding to `peer`.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When `AIN_COMMIT_DELAYED` is used, the number of consecutive calls must not exceed the underlying SQ depth (`sqDepth`). Accumulated tasks must be submitted via `AIN_COMMIT_IMMED` before the SQ is exhausted; otherwise, subsequent tasks will fail due to SQ overflow.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls plus a final `AIN_COMMIT_IMMED`), only the last submission should produce a CQE.
- `Signal` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The calling example of Signal is as follows:

```cpp
extern "C" __global__ __aicore__ void ain_kernel(
      __gm__ void* team, __gm__ void* signalWin,
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
    // Value to be atomically added to the signal
    uint64_t value = 1;

    // Atomically increment the remote signal by 1 in each call
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0}, hcommUbuf);

    // Atomically add value to the remote signal
    ain.Signal<AscendC::AinSignalAdd>(team, peer,
        AscendC::AinSignalAdd{signalWin, 0, value}, hcommUbuf);

    // Wait for communication tasks on all peer channels in the team to complete
    ain.Flush(team);
}
```
