# Signal

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T07:03:29.573Z pushedAt=2026-10-26T07:32:09.947Z nl=2 -->

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

Performs an atomic add operation on the remote signal by specifying the communication team and the peer rank.

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
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means the WQE is assembled and the doorbell is rung immediately for submission; `AIN_COMMIT_DELAYED` means only the WQE is assembled, and a subsequent `AIN_COMMIT_IMMED` task must be called to trigger submission uniformly. |
| `Config` | URMA WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required for the AI Core). |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Handle of the communication team. |
| `peer` | Input | Remote rank ID within the team. |
| `action` | Input | Signal action, supporting `AinSignalInc` and `AinSignalAdd`. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

**Table 3** Signal action description

| Signal Action | Description |
| --- | --- |
| `AinSignalInc{signalWindow, signalOffset}` | Performs an atomic add by 1 on the remote signal. |
| `AinSignalAdd{signalWindow, signalOffset, value}` | Performs an atomic add of `value` on the remote signal. |

Here, `signalWindow` is the symmetric window handle where the destination signal resides, `signalOffset` is the byte offset of the signal within `signalWindow`, and `value` is the value for the atomic add.

## Return Value

No return value.

## Usage Constraints

- The host side shall create the team and register the symmetric window containing the signal before the call.
- `team` and `action.signalWindow` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- The address corresponding to `action.signalOffset` must fall within the registered memory range of the signal window corresponding to `peer`.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When using `AIN_COMMIT_DELAYED`, the number of consecutive calls must not exceed the underlying SQ depth (`sqDepth`). The accumulated tasks must be submitted through `AIN_COMMIT_IMMED` before the SQ is exhausted; otherwise, subsequent tasks will fail due to SQ overflow.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls + a final `AIN_COMMIT_IMMED` call), only the last submission should generate a CQE.
- `Signal` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following is an example of calling the **Signal** API.

```cpp
// Initialize the UBuf workspace size: it must be no less than 512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* signalWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        HCOMM_BUF_SIZE, 0U};
    // Create an Ain object
    AscendC::Ain ain;
    // rankId of the peer end
    uint32_t peer = (rankId + 1) % rankNum;
    // Value to be atomically added to the signal
    uint64_t value = 1U;

    // Perform an atomic increment operation on the remote signal, adding 1 per call
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);

    // Perform an atomic add operation of value on the remote signal
    ain.Signal<AscendC::AinSignalAdd>(team, peer,
        AscendC::AinSignalAdd{signalWin, 0U, value}, hcommUbuf);

    // Wait for the communication tasks of all peer channels in the team to complete
    ain.Flush(team);
}
```
