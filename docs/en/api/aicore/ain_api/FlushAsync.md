# FlushAsync

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

Get the communication channel handle corresponding to a specified peer for subsequent `Wait` calls to wait for communication tasks on that channel to complete.

`FlushAsync` itself does not block waiting for communication task completion. It only resolves the channel handle based on `team`, `peer` and the `contextIndex` of the `Ain` instance, and writes it to `outChannelHandle`.

## Function Prototype

```cpp
__aicore__ inline void FlushAsync(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::ChannelHandle* outChannelHandle);
```

## Parameter Description

**Table 1**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |
| `peer` | Input | Peer rank ID in the team. |
| `outChannelHandle` | Output | Returned communication channel handle. |

## Return Value

No return value.

## Constraints

- The Host side shall create the team before invocation.
- `team` must be a valid device-side handle. `peer` must be a valid rank ID in the team.
- `outChannelHandle` must point to a valid writable address.
- `FlushAsync` does not wait for task completion. The caller needs to call `Wait` to wait for tasks on the channel corresponding to `outChannelHandle` to complete.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The calling example of FlushAsync is as follows:

```cpp
// Size of data transferred in a single communication
constexpr uint64_t BYTES = 1024;

extern "C" __global__ __aicore__ void ain_kernel(
      __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin,
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
    // Submit a one-sided write task
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    AscendC::ChannelHandle channelHandle;
    // Obtain the communication channel handle of the specified peer
    ain.FlushAsync(team, peer, &channelHandle);
    // Wait for communication tasks on this channel to complete
    ain.Wait(channelHandle);
}
```
