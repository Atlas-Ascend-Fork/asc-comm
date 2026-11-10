# Get

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:13:28.718Z pushedAt=2026-11-09T06:47:54.953Z nl=2 -->

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

Submits a one-sided read task by specifying the communication team and the peer rank ID, reading data from the peer symmetric window into the local symmetric window.

`Get` resolves the communication channel based on `team`, `peer` and the `contextIndex` of the `Ain` instance, resolves local/peer GM (Global Memory) addresses from `dstWin`, `srcWin` and offsets, and submits the communication task through the underlying Hcomm read API.

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
| `DescriptorUbuf` | UBuf (Unified Buffer) temporary workspace descriptor type, defaulting to `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` triggers submission immediately after the WQE (Work Queue Element) is assembled; `AIN_COMMIT_DELAYED` only assembles the WQE, and a subsequent `AIN_COMMIT_IMMED` task is required to trigger submission uniformly. |
| `Config` | `COMM_PROTOCOL_UB_CTP` WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order execution + CQE in-order reporting + fence enabled + CQE reporting required). A custom `Config` can adjust fields such as `cqe`. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Remote rank ID within the team. |
| `dstWin` | Input | Local destination symmetric window handle. |
| `dstOffset` | Input | Byte offset within the destination symmetric window. |
| `srcWin` | Input | Remote source symmetric window handle. |
| `srcOffset` | Input | Byte offset within the source symmetric window. |
| `bytes` | Input | Read length, in bytes. |
| `ubuf` | Input | UBuf (Unified Buffer) temporary workspace descriptor used by the underlying Hcomm. |

The type `AinDescriptorUbuf` of `ubuf` contains three fields: `addr` (`__ubuf__ uint8_t*`, start address of the UBuf workspace), `bytes` (`uint32_t`, workspace size in bytes), and `eventId` (`uint32_t`, reserved event ID for synchronization with the underlying communication engine, currently fixed to `0`).

## Return Value

None.

## Usage Constraints

- The host side shall create the team and register symmetric windows before the call.
- `team`, `dstWin` and `srcWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + bytes` must fall within the registered memory range of `dstWin` corresponding to the local rank. `srcOffset + bytes` must fall within the registered memory range of `srcWin` corresponding to `peer`.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B with a start address aligned to 32 bytes.
- When `AIN_COMMIT_DELAYED` is used, this task does not trigger submission immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When `AIN_COMMIT_DELAYED` is used, the number of consecutively accumulated task slots must not exceed the task submission capacity of the channel (that is, the underlying SQ (Send Queue) depth sqDepth). Accumulated tasks must be submitted through `AIN_COMMIT_IMMED` before the capacity is exhausted; otherwise, subsequent tasks fail due to SQ overflow. `Put`/`PutValue`/`Get` each occupy one task slot; `Signal` occupies two task slots.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls followed by a final `AIN_COMMIT_IMMED` call), only the final submission should generate a CQE (Completion Queue Entry). Set the `cqe` field in the `Config` of intermediate tasks to 0 and that of the final task to 1.
- `Get` submits a non-blocking communication task. Call [Flush](./Flush.md) to wait for all peer channel tasks in the team to complete, or call [FlushAsync](./FlushAsync.md) to obtain the specified peer channel and then call [Wait](./Wait.md) to wait for completion.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported. For a single `Get` call, the value range of the `bytes` parameter is `0 < bytes <= 256 * 1024 * 1024`.

## Call Example

The following is an example of calling the **Get** API. For the complete sample, see [01_basic_ring](../../../../../examples/aicore/ain/01_basic_ring/README.md).

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf (Unified Buffer) workspace size: must be no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_get_kernel(
    GM_ADDR team, GM_ADDR dstWin, GM_ADDR srcWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];
    AscendC::AinDescriptorUbuf hcommUbuf{hcommLocal, HCOMM_BUF_SIZE, 0U};
    // Create the Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // One-sided read: read data from the peer srcWin to the local dstWin.
    ain.Get(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES, hcommUbuf);
    // Wait for the communication tasks of all peer channels in the team to complete.
    ain.Flush(team);
}
```
