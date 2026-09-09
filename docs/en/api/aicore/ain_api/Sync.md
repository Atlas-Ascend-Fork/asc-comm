# Sync

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

Perform barrier synchronization within the team bound to `AinBarrierSession`.

`Sync` first sends barrier signals to other ranks in the team, and then waits for other ranks to write the barrier signal of the current rank. The no-timeout overload waits until synchronization completes. The timeout overload returns failure when the number of polling attempts exceeds `timeoutCycles`.

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

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `DescriptorUbuf` | UBuf temporary workspace descriptor type. Default: `AinDescriptorUbuf`. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `order` | Input | Memory order for barrier signal wait. Currently supports `AIN_MEMORY_ORDER_RELAX`. |
| `timeoutCycles` | Input | Maximum number of polling attempts. Used only by the timeout overload. |
| `ubuf` | Input | UBuf temporary workspace descriptor used by the underlying Hcomm. |

## Return Value

The no-timeout overload has no return value.

The timeout overload returns:

| Return Value | Description |
| --- | --- |
| `0` | Synchronization succeeded. |
| `-1` | Waiting exceeded `timeoutCycles`; synchronization failed. |

## Constraints

- A valid `AinBarrierSession` must be constructed before invocation.
- The Host side shall create the team and prepare the synchronization memory used by barrier before invocation.
- `ubuf.addr` and `ubuf.bytes` must provide a UBuf temporary workspace for underlying Hcomm initialization. `DescriptorUbuf` currently must provide a UBuf temporary workspace no smaller than 512 B.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee memory ordering for ordinary data accesses before and after signal operations.
- The no-timeout overload keeps polling until all other ranks arrive at the barrier. The caller must ensure that ranks in the team call it symmetrically to avoid permanent Kernel waiting.
- For the timeout overload, `timeoutCycles` indicates the number of polling attempts. Returning `-1` only indicates that the current rank timed out while waiting, and does not roll back signals that have already been sent.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The calling example of Sync is as follows:

```cpp
constexpr int32_t SYNC_SUCCESS = 0;
constexpr int32_t TIME_OUT_ERROR = -1;

extern "C" __global__ __aicore__ void ain_kernel(__gm__ void* team)
{
    // Prepare the UBuf workspace
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        AscendC::HCOMM_URMA_TMP_BUF_SIZE, 0U};
    // Barrier resource index
    uint32_t index = 0;
    // Create an Ain object
    AscendC::Ain ain(index);
    AscendC::AinBarrierSession barrierSession(&ain, team, index);

    // Barrier synchronization without timeout
    barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, hcommUbuf);

    // Barrier synchronization with timeout
    int32_t ret = barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 10000, hcommUbuf);
    if (ret != 0) {
        return TIME_OUT_ERROR;
    }
    return SYNC_SUCCESS;
}
```
