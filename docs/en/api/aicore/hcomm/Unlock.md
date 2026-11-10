# Unlock

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:54:18.827Z pushedAt=2026-11-09T06:47:54.978Z nl=2 -->

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

Header file path: `"hcomm/hcomm.h"`.

Releases a communication channel lock acquired by [Lock](./Lock.md).

## Function Prototype

```cpp
__aicore__ inline int32_t Unlock(AscendC::ChannelHandle channel);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |

## Return Value

**Table 2** Return values

| Return Value | Description |
| --- | --- |
| `0` | The lock was released successfully. |
| `-1` | The lock could not be released. |

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- Call `Unlock` only after [Lock](./Lock.md) succeeds, and release the lock on the same AI Core that acquired it.
- Do not call `Unlock` repeatedly for the same lock acquisition.
- Use this API only to synchronize AI Cores sharing the same channel on the same device.

## Call Example

The following shows how to call the **Unlock** API. To focus on the call flow, the example omits return value checks and error handling. For the complete sample, see [07_hcomm_roce_normal_write_read](../../../../../examples/aicore/hcomm/07_hcomm_roce_normal_write_read/README.md).

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Size of the UB temporary workspace to initialize: must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_unlock_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace. alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Obtain the channel lock through Lock.
    hcomm.Lock(channel);

    // Submit the communication task within the lock.
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // After the communication task completes, the same AI Core that acquired the lock releases it.
    hcomm.Unlock(channel);
}
```
