# Get

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

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |
| `CommitFlags` | Task submission mode. `AIN_COMMIT_IMMED` means assembling the WQE and ringing the doorbell immediately. `AIN_COMMIT_DELAYED` means assembling only the WQE, and requires a subsequent `AIN_COMMIT_IMMED` task to be called to trigger submission. |
| `Config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG`(Relax Order(RO) + in order CQE + FenceOn + CqeOn). |

**Table 2**  API parameters

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

## Return Value

No return value.

## Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- `team`, `dstWin` and `srcWin` must be valid device-side handles. `peer` must be a valid rank ID in the team.
- `dstOffset + bytes` must fall within the registered memory range of `dstWin` corresponding to the local rank. `srcOffset + bytes` must fall within the registered memory range of `srcWin` corresponding to `peer`.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- When `AIN_COMMIT_DELAYED` is used, this task does not ring the doorbell immediately. At least one subsequent `AIN_COMMIT_IMMED` task must be submitted to ensure that previous tasks are submitted.
- When `AIN_COMMIT_DELAYED` is used, the number of consecutive calls must not exceed the underlying SQ depth (`sqDepth`). Accumulated tasks must be submitted via `AIN_COMMIT_IMMED` before the SQ is exhausted; otherwise, subsequent tasks will fail due to SQ overflow.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls plus a final `AIN_COMMIT_IMMED`), only the last submission should produce a CQE.
- `Get` submits a non-blocking communication task. Call `Flush` to wait for tasks on all peer channels in the team to complete, or call `FlushAsync` to get a specified peer channel and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported. For a single `Get` call, the valid range of the input parameter `bytes` is `0 < bytes <= 256 * 1024 * 1024`.

## Example

The calling example of Get is as follows:

```cpp
AscendC::Ain<> ain;
ain.Get(team, peer, dstWin, dstOffset, srcWin, srcOffset, bytes);
ain.Flush(team);    // Wait for communication tasks on all peer channels in the team to complete
```
