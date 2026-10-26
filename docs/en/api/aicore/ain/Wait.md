# Wait

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T07:03:59.841Z pushedAt=2026-10-26T07:32:09.947Z nl=2 -->

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

Waits for communication tasks on a specified communication channel to complete.

Typically, call `FlushAsync` first to obtain the communication channel handle of a specified peer, and then call `Wait` to wait for submitted tasks on that channel to complete.

## Function Prototype

```cpp
__aicore__ inline void Wait(AscendC::ChannelHandle& channelHandle);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channelHandle` | Input | Communication channel handle, usually returned by `FlushAsync`. |

## Return Value

No return value.

## Usage Constraints

- `channelHandle` must be a valid communication channel handle.
- `Wait` blocks and waits for submitted tasks on this channel to complete through the underlying Hcomm completion-wait API.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following shows an example of calling the **Wait** API.

```cpp
// Data size of a single communication transfer.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf workspace size: must not be less than 512B.
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
    // Create an Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // Submit a one-sided write task.
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    AscendC::ChannelHandle channelHandle{0U};
    // FlushAsync obtains the communication channel handle corresponding to the specified peer.
    ain.FlushAsync(team, peer, &channelHandle);
    // Block and wait for the communication task on this channel to complete.
    ain.Wait(channelHandle);
}
```
