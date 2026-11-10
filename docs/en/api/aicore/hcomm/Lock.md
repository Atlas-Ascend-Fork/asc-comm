# Lock

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:35:06.484Z pushedAt=2026-11-09T06:47:54.968Z nl=2 -->

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

Acquires the lock of a communication channel when multiple AI Cores share the same channel. With a valid `channel`, if the lock is already held, the call waits until the lock is acquired and does not fail due to lock contention.

## Function Prototype

```cpp
__aicore__ inline int32_t Lock(AscendC::ChannelHandle channel);
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
| `0` | The lock was acquired successfully. |
| `-1` | `channel` is null or misaligned. Lock contention does not return `-1`. |

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- The lock cannot be reentered. Do not call `Lock` again for the same channel before unlock.
- Every successful call to `Lock` must be paired with [Unlock](./Unlock.md) on the same AI Core, including unlock on error paths.
- Use this API only to synchronize AI Cores sharing the same channel on the same device.

## Call Example

The following shows how to call the **Lock** API. To focus on the call flow, the example omits return value checks and error handling. For the complete sample, see [07_hcomm_roce_normal_write_read](../../../../../examples/aicore/hcomm/07_hcomm_roce_normal_write_read/README.md).

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Size of the UB temporary workspace to initialize: must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_lock_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // When multiple AI Cores share the same channel, obtain the channel lock first.
    hcomm.Lock(channel);

    // Submit the communication task after the lock is obtained successfully.
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // Even if the communication API returns a failure, release the lock that has been obtained successfully.
    hcomm.Unlock(channel);
}
```
