# Flush

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T06:56:19.468Z pushedAt=2026-10-26T07:32:09.931Z nl=2 -->

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
| `team` | Input | Handle of the communication team. |

## Return Value

No return value.

## Usage Constraints

- The host side shall create the team before the call.
- `team` must be a valid device-side handle.
- `Flush` waits for submitted tasks on all peer channels in the team to complete. To wait for a single peer channel, call `FlushAsync` to obtain the channel handle and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following is an example of calling the **Flush** API.

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf workspace size: it must not be less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin, __gm__ void* signalWin,
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
    // Create an Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // Submit a one-sided write task.
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // Perform atomic increment by 1 on the remote signal.
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);
    // Block and wait until communication tasks on all peer channels in the team are complete.
    ain.Flush(team);
}
```
