# Get

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T06:57:48.735Z pushedAt=2026-10-26T07:32:09.942Z nl=2 -->

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

Submits a one-sided read task by specifying the communication team and the peer rank, reading data from the peer symmetric window into the local symmetric window.

`Get` resolves the communication channel based on `team`, `peer` and the `contextIndex` of the `Ain` instance, resolves local/peer GM addresses from `dstWin`, `srcWin` and offsets, and submits the communication task through the underlying Hcomm read API.

## Function Prototype

```cpp
template <
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_DEFAULT_CFG>
__aicore__ inline void Get(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::HcclCommSymWindow dstWin,
    uint64_t dstOffset,
    AscendC::HcclCommSymWindow srcWin,
    uint64_t srcOffset,
    uint64_t bytes,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means the WQE is assembled and the doorbell is rung immediately for submission; `AIN_COMMIT_DELAYED` means only the WQE is assembled, and a subsequent `AIN_COMMIT_IMMED` task must be called to trigger submission uniformly. |
| `Config` | URMA WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required for the AI Core). |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Handle of the communication team. |
| `peer` | Input | Remote rank ID within the team. |
| `dstWin` | Input | Local destination symmetric window handle. |
| `dstOffset` | Input | Byte offset within the destination symmetric window. |
| `srcWin` | Input | Remote source symmetric window handle. |
| `srcOffset` | Input | Byte offset within the source symmetric window. |
| `bytes` | Input | Read length, in bytes. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Return Value

No return value.

## Usage Constraints

- The host side shall create the team and register symmetric windows before the call.
- `team`, `dstWin` and `srcWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + bytes` must fall within the registered memory range of `dstWin` corresponding to the local rank. `srcOffset + bytes` must fall within the registered memory range of `srcWin` corresponding to `peer`.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When using `AIN_COMMIT_DELAYED`, the number of consecutive calls must not exceed the underlying SQ depth (`sqDepth`). The accumulated tasks must be submitted through `AIN_COMMIT_IMMED` before the SQ is exhausted; otherwise, subsequent tasks will fail due to SQ overflow.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls + a final `AIN_COMMIT_IMMED` call), only the last submission should generate a CQE.
- `Get` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported. For a single `Get` call, the value range of the `bytes` parameter is `0 < bytes <= 256 * 1024 * 1024`.

## Example

The following is an example of calling the **Get** API.

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf workspace size: must be no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace.
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        HCOMM_BUF_SIZE, 0U};
    // Create the Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // One-sided read: read data from the peer srcWin to the local dstWin.
    ain.Get(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES, hcommUbuf);
    // Wait for all peer channel communication tasks in the team to complete.
    ain.Flush(team);
}
```
