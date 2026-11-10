# asccomm_write_reduce_on_thread

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-04T10:16:15.199Z pushedAt=2026-11-09T06:47:54.995Z nl=2 -->

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

Initiates a reduction write on the specified channel: writes `count` elements of type `dataType` from the local `src` to the remote `dst`, computes them with the corresponding elements in `dst` according to `reduceOp`, and writes the result back to `dst`. The API caller is the node where `src` resides.

## Function Prototype

```cpp
int32_t asccomm_write_reduce_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, AsccommDataType dataType, AsccommReduceOp reduceOp)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | -------------------------------------- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| channel | Input | Communication channel handle. After the control plane creates the channel resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ChannelHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| dst | Output | Remote destination address, used to store the reduction result. |
| src | Input | Local source address, providing the data to be written. |
| count | Input | Number of elements. |
| dataType | Input | Data type. The type is `AsccommDataType`, defined in `"aicpu/hcomm/asccomm_res_defs.h"`.<br>Currently only the Ascend 950PR&950DT products support this API. For the behavior of each value, see [dataType Description](#datatype-description). |
| reduceOp | Input | Reduction operation type. The type is `AsccommReduceOp`, defined in `"aicpu/hcomm/asccomm_res_defs.h"`.<br>For the support matrix, see [reduceOp Description](#reduceop-description). |

### dataType Description

Only the Ascend 950PR&950DT products support this API. In the current implementation, the behavior of each `AsccommDataType` value is as follows.

| dataType | [UB Protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection) | [RoCE Protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection) |
| --- | --- | --- |
| `ASCCOMM_DATA_TYPE_INT8`, `ASCCOMM_DATA_TYPE_INT16`, `ASCCOMM_DATA_TYPE_INT32`, `ASCCOMM_DATA_TYPE_FP16`, `ASCCOMM_DATA_TYPE_FP32` | Reduction write is supported. | Reduction write is supported. |
| `ASCCOMM_DATA_TYPE_UINT8`, `ASCCOMM_DATA_TYPE_UINT16`, `ASCCOMM_DATA_TYPE_UINT32`, `ASCCOMM_DATA_TYPE_BFP16` | Reduction write is supported. | Not supported. The current implementation returns `ASCCOMM_E_PARA`. |
| `ASCCOMM_DATA_TYPE_INT64`, `ASCCOMM_DATA_TYPE_UINT64` | Not supported. The current implementation returns `ASCCOMM_E_INTERNAL`. | Not supported. Returns `ASCCOMM_E_NOT_SUPPORT`. |
| `ASCCOMM_DATA_TYPE_FP64`, `ASCCOMM_DATA_TYPE_INT128` | Not supported. The current implementation returns `ASCCOMM_E_INTERNAL`. | Not supported. The current implementation returns `ASCCOMM_E_PARA`. |
| `ASCCOMM_DATA_TYPE_HIF8`, `ASCCOMM_DATA_TYPE_FP8E4M3`, `ASCCOMM_DATA_TYPE_FP8E5M2`, `ASCCOMM_DATA_TYPE_FP8E8M0`, `ASCCOMM_DATA_TYPE_RESERVED` | Not accepted by the current implementation. Returns `ASCCOMM_E_PARA`. | Not accepted by the current implementation. Returns `ASCCOMM_E_PARA`. |

### reduceOp Description

`AsccommReduceOp` contains `ASCCOMM_REDUCE_SUM`, `ASCCOMM_REDUCE_PROD`, `ASCCOMM_REDUCE_MAX`, `ASCCOMM_REDUCE_MIN`, and `ASCCOMM_REDUCE_RESERVED`. The current implementation supports each channel protocol as follows.

| reduceOp | UB Protocol | RoCE Protocol |
| --- | --- | --- |
| `ASCCOMM_REDUCE_SUM` | Reduction write is supported. | Reduction write is supported. |
| `ASCCOMM_REDUCE_PROD` | Not supported. The current implementation returns `ASCCOMM_E_INTERNAL`. | Not supported. Returns `ASCCOMM_E_NOT_SUPPORT`. |
| `ASCCOMM_REDUCE_MAX` | Reduction write is supported. | Reduction write is supported. |
| `ASCCOMM_REDUCE_MIN` | Reduction write is supported. | Reduction write is supported. |
| `ASCCOMM_REDUCE_RESERVED` | Invalid reserved value. Returns `ASCCOMM_E_PARA`. | Invalid reserved value. Returns `ASCCOMM_E_PARA`. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The reduction write request is processed successfully. For the UB protocol, it indicates that the reduction write task has been submitted. For the RoCE protocol, the completion result of the reduction write operation is checked before returning. |
| `ASCCOMM_E_PTR` | `dst` or `src` is null, the thread or channel handle is 0, or the required resource address is null. |
| `ASCCOMM_E_PARA` | `dataType` or `reduceOp` is invalid, `count * sizeof(dataType)` overflows, or the thread/channel resources or memory protection parameters are invalid. |
| `ASCCOMM_E_NOT_SUPPORT` | The channel protocol is not supported, or RoCE inline reduction does not support the specified data type or reduction operation. |
| `ASCCOMM_E_TIMEOUT` | The communication task submission queue or RoCE queue times out, or the RoCE completion event polling times out. |
| `ASCCOMM_E_REMOTE` | The RoCE completion event reports a remote communication error. |
| `ASCCOMM_E_INTERNAL` | An exception occurs in UB reduction task encoding, task submission, or other internal operations. |

## Usage Constraints

- This API supports both the UB protocol and the RoCE protocol.
- `src` and `dst` should use the local and remote communication memory addresses corresponding to the channel.
- The `dst` address range must fall within the remote registered communication memory range.
- `ASCCOMM_E_PARA` is returned when `count * sizeof(dataType)` overflows.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in from the control plane; channel is a valid ChannelHandle resource handle passed in from the control plane.
// src is the local communication memory address; dst is the remote communication memory address.
uint64_t count = 256;
int32_t ret = asccomm_write_reduce_on_thread(
    thread, channel, dst, src, count, ASCCOMM_DATA_TYPE_FP32, ASCCOMM_REDUCE_SUM);
```
