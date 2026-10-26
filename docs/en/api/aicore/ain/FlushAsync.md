# FlushAsync

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T06:57:00.073Z pushedAt=2026-10-26T07:32:09.932Z nl=2 -->

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

Obtains the communication channel handle corresponding to a specified peer for subsequent `Wait` calls to wait for communication tasks on that channel to complete.

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
| `peer` | Input | Remote rank ID within the team. |
| `outChannelHandle` | Output | Returns the resolved communication channel handle. |

## Return Value

No return value.

## Usage Constraints

- The host side shall create the team before the call.
- `team` must be a valid device-side handle. `peer` must be a valid rank ID in the team.
- `outChannelHandle` must point to a valid writable address.
- `FlushAsync` does not wait for task completion. The caller needs to call `Wait` to wait for tasks on the channel corresponding to `outChannelHandle` to complete.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following is an example of calling the **FlushAsync** API.

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf workspace size: it must be no less than 512B.
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
    // Submit the one-sided write task.
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    AscendC::ChannelHandle channelHandle{0U};
    // Obtain the communication channel handle corresponding to the specified remote end.
    ain.FlushAsync(team, peer, &channelHandle);
    // Wait for the communication task on this channel to complete.
    ain.Wait(channelHandle);
}
```
