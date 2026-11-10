# Init

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:45:23.942Z pushedAt=2026-11-09T06:47:54.974Z nl=2 -->

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

Header file path: `"hcomm/hcomm_simt.h"`.

Initializes the SIMT **Hcomm** API. Under the `COMM_PROTOCOL_UB_CTP` protocol, the SIMT API does not require a temporary workspace, so pass `nullptr` for `buff` and **0** for `len`.

## Function Prototype

```cpp
__simt_callee__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `buff` | Input | Pointer to the temporary workspace. Pass `nullptr` under the `COMM_PROTOCOL_UB_CTP` protocol. |
| `len` | Input | Length of the temporary workspace, in bytes. Pass **0** under the `COMM_PROTOCOL_UB_CTP` protocol. |

## Return Value

**Table 2** Return values

| Return Value | Description |
| --- | --- |
| `0` | Initialization succeeded. |
| `-1` | Initialization failed. |

## Usage Constraints

- Complete initialization before submitting communication tasks.
- The shared UB (Unified Buffer) workspace used for group batch collaboration is passed through the `buff` parameter of [MakeBatchHandle](./SIMT_MakeBatchHandle.md), not through this API.

## Call Example

The following shows an example of calling the **Init** API. To focus on the call flow, the example omits return value checks and error handling. For the complete sample, see [04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md).

```cpp
AscendC::simt::Hcomm<> hcomm;
hcomm.Init(nullptr, 0);
```
