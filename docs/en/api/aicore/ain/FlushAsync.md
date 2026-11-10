# FlushAsync

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:12:27.507Z pushedAt=2026-11-09T06:47:54.951Z nl=2 -->

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

Obtains the communication channel handle corresponding to a specified peer rank ID for subsequent `Wait` calls to wait for communication tasks on that channel to complete.

`FlushAsync` itself does not block waiting for communication task completion. It only resolves the channel handle based on `team`, `peer` and the `contextIndex` of the `Ain` instance, and writes it to `outChannelHandle`.

## Function Prototype

```cpp
__aicore__ inline void FlushAsync(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::ChannelHandle* outChannelHandle);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Handle of the communication team. |
| `peer` | Input | Peer rank ID in the team. |
| `outChannelHandle` | Output | Returns the resolved communication channel handle. |

## Return Value

None.

## Usage Constraints

- The host side shall create the team before the call.
- `team` must be a valid device-side handle. `peer` must be a valid rank ID in the team.
- `outChannelHandle` must point to a valid writable address.
- `FlushAsync` does not wait for task completion. The caller needs to call [Wait](./Wait.md) to wait for tasks on the channel corresponding to `outChannelHandle` to complete.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.

## Call Example

The following is an example of calling the **FlushAsync** API.

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf (Unified Buffer) workspace size: no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_flush_async_kernel(
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
    // Submit the one-sided write task.
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    AscendC::ChannelHandle channelHandle{0U};
    // Obtain the communication channel handle corresponding to the specified peer rank ID.
    ain.FlushAsync(team, peer, &channelHandle);
    // Wait for the communication tasks on this channel to complete.
    ain.Wait(channelHandle);
}
```
