# Wait

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

Wait for communication tasks on a specified communication channel to complete.

Typically, call `FlushAsync` first to obtain the communication channel handle of a specified peer, and then call `Wait` to wait for submitted tasks on that channel to complete.

## Function Prototype

```cpp
__aicore__ inline void Wait(AscendC::ChannelHandle& channelHandle);
```

## Parameter Description

**Table 1**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channelHandle` | Input | Communication channel handle, usually returned by `FlushAsync`. |

## Return Value

No return value.

## Constraints

- `channelHandle` must be a valid communication channel handle.
- `Wait` blocks and waits for submitted tasks on this channel to complete through the underlying Hcomm completion-wait API.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.

## Example

The calling example of Wait is as follows:

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
    // FlushAsync obtains the communication channel handle of the specified peer
    ain.FlushAsync(team, peer, &channelHandle);
    // Block until communication tasks on this channel are complete
    ain.Wait(channelHandle);
}
```
