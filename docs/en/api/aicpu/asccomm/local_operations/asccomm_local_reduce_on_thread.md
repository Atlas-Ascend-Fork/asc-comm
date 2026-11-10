# asccomm_local_reduce_on_thread

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:05:35.624Z pushedAt=2026-11-09T06:47:54.986Z nl=2 -->

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

Header file path: `"aicpu/hcomm/asccomm_primitives.h"`.

Provides local reduction: computes the `count` elements of type `dataType` in `src` with the corresponding elements in `dst` according to `reduceOp`, and writes the results back to `dst`.

## Function Prototype

```cpp
int32_t asccomm_local_reduce_on_thread(ThreadHandle thread, void *dst, const void *src, uint64_t count, AsccommDataType dataType, AsccommReduceOp reduceOp)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be **0** when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| dst | Output | Destination address in device memory. |
| src | Input | Source address in device memory. |
| count | Input | Number of elements. When `count` is **0**, no reduction task is generated. |
| dataType | Input | Data type. The type is `AsccommDataType`, defined in `"aicpu/hcomm/asccomm_res_defs.h"`. For the support status of each value, see [dataType Description](#datatype-description). |
| reduceOp | Input | Reduction operation type. For the support matrix, see [reduceOp Description](#reduceop-description). |

`AsccommDataType` and `AsccommReduceOp` are defined in `"aicpu/hcomm/asccomm_res_defs.h"`.

### dataType Description

In the current implementation, the local reduction support status of each `AsccommDataType` value is as follows.

| dataType | Local Reduction | Return Value When Not Supported or Invalid |
| --- | --- | --- |
| `ASCCOMM_DATA_TYPE_INT8`, `ASCCOMM_DATA_TYPE_INT16`, `ASCCOMM_DATA_TYPE_INT32`, `ASCCOMM_DATA_TYPE_FP16`, `ASCCOMM_DATA_TYPE_FP32`, `ASCCOMM_DATA_TYPE_BFP16` | Supported | Not applicable. |
| `ASCCOMM_DATA_TYPE_INT64`, `ASCCOMM_DATA_TYPE_UINT64`, `ASCCOMM_DATA_TYPE_UINT8`, `ASCCOMM_DATA_TYPE_UINT16`, `ASCCOMM_DATA_TYPE_UINT32`, `ASCCOMM_DATA_TYPE_FP64`, `ASCCOMM_DATA_TYPE_INT128` | Not supported | `ASCCOMM_E_NOT_SUPPORT`. |
| `ASCCOMM_DATA_TYPE_HIF8`, `ASCCOMM_DATA_TYPE_FP8E4M3`, `ASCCOMM_DATA_TYPE_FP8E5M2`, `ASCCOMM_DATA_TYPE_FP8E8M0`, `ASCCOMM_DATA_TYPE_RESERVED` | Not accepted in the current implementation | `ASCCOMM_E_PARA`. |

### reduceOp Description

`AsccommReduceOp` includes `ASCCOMM_REDUCE_SUM`, `ASCCOMM_REDUCE_PROD`, `ASCCOMM_REDUCE_MAX`, `ASCCOMM_REDUCE_MIN`, and `ASCCOMM_REDUCE_RESERVED`. The current support for local reduction is as follows.

| reduceOp | Local Reduction | Return Value When Not Supported or Invalid |
| --- | --- | --- |
| `ASCCOMM_REDUCE_SUM` | Supported | Not applicable. |
| `ASCCOMM_REDUCE_PROD` | Not supported | `ASCCOMM_E_NOT_SUPPORT`. |
| `ASCCOMM_REDUCE_MAX` | Supported | Not applicable. |
| `ASCCOMM_REDUCE_MIN` | Supported | Not applicable. |
| `ASCCOMM_REDUCE_RESERVED` | Illegal reserved value | `ASCCOMM_E_PARA`. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The reduction task has been generated (no task is generated when `count` is **0**), which does not mean that the reduction operation has completed. |
| `ASCCOMM_E_PTR` | The thread handle is 0, the source or destination address is null, or the thread internal task buffer address is null. |
| `ASCCOMM_E_PARA` | The data type or reduction operation is illegal, the total data length calculation overflows, or the thread/communication task submission queue parameters are invalid. |
| `ASCCOMM_E_NOT_SUPPORT` | The current local reduction does not support the specified reduction operation or data type. |
| `ASCCOMM_E_TIMEOUT` | The communication task submission queue has insufficient space, and waiting for available space timed out. |
| `ASCCOMM_E_INTERNAL` | Internal operations such as obtaining the task ID, querying or configuring the communication task submission queue, or submitting the reduction task failed. |

## Usage Constraints

- **dst** and **src** must be non-NULL and valid device addresses directly accessible by the AI CPU.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed from the control plane; src and dst are device addresses directly accessible by the AI CPU.
uint64_t count = 256;
int32_t ret = asccomm_local_reduce_on_thread(
    thread, dst, src, count, ASCCOMM_DATA_TYPE_FP32, ASCCOMM_REDUCE_SUM);
```
