# Sync

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T07:03:51.741Z pushedAt=2026-10-26T07:32:09.948Z nl=2 -->

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

Performs barrier synchronization within the team bound to `AinBarrierSession`.

`Sync` first sends barrier signals to other ranks in the team, and then waits for other ranks to write the barrier signal of the local rank. The no-timeout overload waits until synchronization completes. The timeout overload returns failure when the number of polling attempts exceeds `timeoutCycles`.

## Function Prototype

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline void Sync(
    AscendC::AinMemoryOrder order,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline int32_t Sync(
    AscendC::AinMemoryOrder order,
    uint64_t timeoutCycles,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `order` | Input | Memory order for barrier signal wait. Currently, `AIN_MEMORY_ORDER_RELAX` is supported. |
| `timeoutCycles` | Input | Maximum number of polling attempts. It is used only by the timeout overload. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Return Value

The no-timeout overload has no return value.

The timeout overload returns:

| Return Value | Description |
| --- | --- |
| `0` | Synchronization succeeded. |
| `-1` | Waiting exceeded `timeoutCycles`; synchronization failed. |

## Usage Constraints

- A valid `AinBarrierSession` must be constructed before the call.
- The host side shall create the team and prepare the synchronization memory used by barrier before the call.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee the memory order for ordinary data accesses before and after signal operations.
- The no-timeout overload keeps polling until all other ranks arrive at the barrier. The caller must ensure that ranks in the team call it symmetrically to avoid permanent kernel waiting.
- For the timeout overload, `timeoutCycles` indicates the number of polling attempts. Returning `-1` only indicates that the local rank timed out while waiting, and does not roll back signals that have already been sent.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following shows an example of calling the **Sync** API.

```cpp
// Size of data transferred in a single communication
constexpr uint64_t BYTES = 1024U;
// Size of the UBuf workspace to initialize: must be no less than 512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        HCOMM_BUF_SIZE, 0U};
    // Barrier resource index
    uint32_t index = 0U;
    // Create an Ain object
    AscendC::Ain ain(index);
    AscendC::AinBarrierSession barrierSession(&ain, team, index);
    // Barrier synchronization with timeout
    int32_t ret = barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 10000U, hcommUbuf);
    if (ret != 0) {
        return;
    }
    // Peer rankId
    uint32_t peer = (rankId + 1) % rankNum;
    // One-sided write: write data from the local srcWin to the peer dstWin
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // Barrier synchronization without timeout
    barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
}
```
