# Flush

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:10:51.645Z pushedAt=2026-11-09T06:47:54.949Z nl=2 -->

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

Waits for the communication tasks on all peer channels in the specified communication team to complete.

`Flush` traverses all ranks in the team except the current rank, resolves the corresponding communication channel based on the `contextIndex` of the `Ain` instance, and calls the underlying Hcomm completion-wait API on each channel.

## Function Prototype

```cpp
__aicore__ inline void Flush(AscendC::HcommTeamHandle team);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |

## Return Value

None.

## Usage Constraints

- The host side shall create the team before call.
- `team` must be a valid device-side handle.
- `Flush` waits for the submitted tasks on all peer channels in the team to complete. If you only need to wait for a single peer channel, obtain the channel handle through [FlushAsync](./FlushAsync.md) and then call [Wait](./Wait.md).
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.

## Call Example

The following is an example of calling the **Flush** API. For the complete sample, see [01_basic_ring](../../../../../examples/aicore/ain/01_basic_ring/README.md).

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf (Unified Buffer) workspace size: no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_flush_kernel(
    GM_ADDR team, GM_ADDR dstWin, GM_ADDR srcWin, GM_ADDR signalWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];
    AscendC::AinDescriptorUbuf hcommUbuf{hcommLocal, HCOMM_BUF_SIZE, 0U};
    // Create the Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // Submit the one-sided write task.
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // Atomically increment the peer signal by 1.
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);
    // Block and wait until the communication tasks on all peer channels in the team are complete.
    ain.Flush(team);
}
```
