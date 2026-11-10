# Init

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:33:50.816Z pushedAt=2026-11-09T06:47:54.967Z nl=2 -->

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

Initializes the Hcomm temporary workspace used by the common `ChannelHandle` APIs. For both the `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UB_CTP` protocols, the common APIs must call this API before submitting communication tasks.

The **BatchHandle** call chain does not depend on `Init`; its workspace is provided through [MakeBatchHandle](./MakeBatchHandle.md).

## Function Prototype

```cpp
__aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

template <typename T>
__aicore__ inline int32_t Init(const AscendC::LocalTensor<T>& buff, uint32_t len);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `buff` | Input | UB workspace provided by the caller, which can be passed in as a raw pointer or a `LocalTensor`. |
| `len` | Input | Buffer length, in bytes. |

## Return Value

**Table 2** Return values

| Return Value | Description |
| --- | --- |
| `0` | Initialization succeeded. |
| `-1` | Initialization failed. |

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- Both the `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UB_CTP` protocols use `buff` as the temporary workspace required for assembling communication tasks. The current minimum workspace size is 512 bytes, and the start address must be 32-byte aligned.
- When initializing with `__ubuf__ uint8_t*`, pass a 32-byte aligned start address. When initializing with `LocalTensor`, the workspace uses the start address of the passed tensor, and the tensor capacity in bytes is `buff.GetSize() * sizeof(T)`. The `COMM_PROTOCOL_ROCE` protocol requires both `len` and `buff.GetSize() * sizeof(T)` to be no less than 512 bytes. The `COMM_PROTOCOL_UB_CTP` protocol requires `len` to be no less than 512 bytes and no greater than `buff.GetSize() * sizeof(T)`.
- This API is used only for the common `ChannelHandle` call chain. The **BatchHandle** batch APIs do not need to call this API.

## Call Example

The following shows an example of calling the **Init** API. To focus on the call flow, the example omits return value checks and error handling. For the complete sample, see [01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md).

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Size of the UB temporary workspace to initialize: must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_init_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create an Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // After initialization, call the common API to submit communication tasks.
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // Block and wait for the submitted tasks on the channel to complete.
    hcomm.Drain(channel);
}
```
