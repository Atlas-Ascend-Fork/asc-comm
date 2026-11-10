# Wait

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:19:43.302Z pushedAt=2026-11-09T06:47:54.959Z nl=2 -->

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

Waits for communication tasks on a specified communication channel to complete.

Typically, call [FlushAsync](./FlushAsync.md) first to obtain the communication channel handle of the specified peer, and then call `Wait` to wait for the submitted tasks on this channel to complete.

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

None.

## Usage Constraints

- `channelHandle` must be a valid communication channel handle.
- `Wait` blocks and waits for submitted tasks on this channel to complete through the underlying Hcomm completion-wait API.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.

## Call Example

The following shows an example of calling the **Wait** API.

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf (Unified Buffer) workspace size: must be no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_wait_kernel(
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
    // FlushAsync obtains the communication channel handle corresponding to the specified peer.
    ain.FlushAsync(team, peer, &channelHandle);
    // Block and wait for the communication task on this channel to complete.
    ain.Wait(channelHandle);
}
```
