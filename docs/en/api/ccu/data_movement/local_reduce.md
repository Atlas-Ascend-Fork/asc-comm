# local_reduce

<!-- md-trans-meta sourceCommit=cf6edf5a4f7bce7570d44b2464954320a3952923 translatedAt=2026-11-04T10:30:57.392Z pushedAt=2026-11-09T06:47:55.008Z nl=2 -->

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

Header file: `#include "ccu/hcomm/ccu_primitives.hpp"`

Merges the source data with the destination memory or multiple ccu_buffers using the specified operator (asynchronous). When the hardware completes, the flag bit corresponding to `mask` in `evt` is automatically set to **1**.

The supported data paths are:

- Local on-chip memory -> local on-chip memory (in-place reduction: `dst = reduce(dst, src, op_type)`, with the same input and output types)
- N local CCU Buffers -> `buffers[0]` (2 ≤ N ≤ 8, the hardware reduces N paths in one pass; the output type supports the same precision as the input, or promoted precision under `HCCL_REDUCE_SUM`. For details, see the `output_data_type` parameter description)

## Function Prototype

- Local on-chip memory -> local on-chip memory

    ```cpp
    CcuResult local_reduce(local_addr dst, local_addr src, variable len,
                           HcclDataType data_type, HcclReduceOp op_type,
                           event evt, uint16_t mask = 1);
    ```

- N local CCU Buffers -> `buffers[0]` (2 ≤ count ≤ 8)

    ```cpp
    CcuResult local_reduce(ccu_buffer* buffers, uint32_t count,
                           HcclDataType data_type, HcclDataType output_data_type,
                           HcclReduceOp op_type,
                           variable len, event evt, uint16_t mask = 1);
    ```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| dst | Input/Output | Destination on-chip memory address (`local_addr`, local on-chip memory -> local on-chip memory). A valid initial value must be written before the call; it is updated to the reduction result after the hardware completes. |
| src | Input | Source on-chip memory address (`local_addr`, local on-chip memory -> local on-chip memory). |
| buffers | Input/Output | Base address of the ccu_buffer array (`ccu_buffer*`, N local CCU Buffers -> `buffers[0]`). It must not be `nullptr`. You are advised to allocate it through `ccu::array<ccu_buffer>` to ensure physical contiguity. `buffers[0]` is the output location of the reduction result, and a valid initial value must be written before the call. In the precision expansion scenario (low-precision input + SUM promoted-precision output, for example, when INT8→FP32, the output elements are 4× the input), the passed ccu_buffer array must cover both the input and the expanded output footprint (for example, for 2-path INT8 input promoted to FP32 output, 4 ccu_buffers must be reserved instead of 2); insufficient reservation causes hardware read/write out-of-bounds and undefined behavior. |
| count | Input | Number of ccu_buffers (N local CCU Buffers -> `buffers[0]`). The value range is `[2, 8]` (exceeding the upper limit throws an exception with an error code). `count == 0` is rejected directly and returns `CCU_E_PARA`; `count == 1` is not rejected but the hardware behavior is undefined. For a single ccu_buffer scenario, use the "local on-chip memory -> local on-chip memory" path. `count` must equal the actual length of the `buffers` array. |
| data_type | Input | Input data type. For values, see the `HcclDataType` enumeration. Only the following six types are supported: `HCCL_DATA_TYPE_UINT8`, `HCCL_DATA_TYPE_INT16`, `HCCL_DATA_TYPE_INT32`, `HCCL_DATA_TYPE_FP16`, `HCCL_DATA_TYPE_FP32`, and `HCCL_DATA_TYPE_BFP16`. Other values are rejected and an exception is thrown (with an error code). |
| output_data_type | Input | Output data type (only for the "N local CCU Buffers -> `buffers[0]`" path). For values, see the `HcclDataType` enumeration. Two combinations are supported: ① same precision - the value is the same as `data_type`; ② promoted precision - supported only when `op_type == HCCL_REDUCE_SUM`, which reduces low-precision inputs and then promotes them to high-precision output (for example, `HCCL_DATA_TYPE_INT8` → `HCCL_DATA_TYPE_FP32`; see "Call Example - Scenario 2"). Other `data_type`/`output_data_type` combinations return `CCU_E_NOT_SUPPORT` (see the return value table). In the promoted-precision scenario, `buffers` must reserve ccu_buffers according to the precision expansion ratio. For details, see the `buffers` parameter description. |
| op_type | Input | Reduction operator. For values, see the `HcclReduceOp` enumeration. Only `HCCL_REDUCE_SUM`, `HCCL_REDUCE_MAX`, and `HCCL_REDUCE_MIN` are supported; `HCCL_REDUCE_PROD` is not supported and is rejected with an exception (carrying an error code) when passed. |
| len | Input | Number of bytes to operate on, of type `variable`. The actual value is taken from the corresponding general-purpose register at the dispatch stage.<br>&bull;Local on-chip memory -> local on-chip memory: total number of bytes to operate on.<br>&bull;N local CCU Buffers -> `buffers[0]`: number of bytes of each ccu_buffer participating in the reduction, which must not exceed the size of a single slice (4096 bytes). |
| evt | Input | Completion event object. When the hardware reduction completes, the flag bit corresponding to `mask` in `evt` is automatically set to 1, and the downstream calls `event_wait(evt, mask)` to wait. |
| mask | Input | 16-bit event mask, specifying which flag bits of `event` are set when the hardware completes. The default value is `1`, meaning only **bit0** is set. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PARA` | Parameter error, including `buffers` being `nullptr` or `count` being 0. |
| `CCU_E_NOT_SUPPORT` | Only for the "N Local End CCU Buffers -> `buffers[0]`" path: the `data_type`/`output_data_type` combination does not satisfy the higher-precision/same-precision constraint (see the `output_data_type` description for details). |

## Usage Constraints

- The reduction is an in-place operation. Before the call, `dst` (for the "Local On-Chip Memory -> Local On-Chip Memory" path) or `buffers[0]` (for the "N Local End CCU Buffers -> `buffers[0]`" path) must already contain a valid initial value; otherwise, the reduction result is undefined.
- For the "N Local End CCU Buffers -> `buffers[0]`" path, `buffers` must point to a physically contiguous `ccu_buffer` array. It is recommended to allocate it using `ccu::array<ccu_buffer>`.
- For the "N Local End CCU Buffers -> `buffers[0]`" path, `count` ranges from `[2, 8]`. When `count > 8`, an exception is thrown (carrying an error code). When `count == 0`, `CCU_E_PARA` is returned directly. When `count == 1`, it is not rejected, but the hardware behavior is undefined (use the "Local On-Chip Memory -> Local On-Chip Memory" path instead).
- For the "N Local End CCU Buffers -> `buffers[0]`" path, in a precision expansion scenario (low-precision input + SUM higher-precision output), `buffers` must reserve additional ccu_buffer entries according to the expansion ratio to accommodate the expanded output; otherwise, the hardware reads/writes out of bounds and the behavior is undefined.
- For the "N Local End CCU Buffers -> `buffers[0]`" path, `len` must not exceed the size of a single `ccu_buffer` (4096 bytes). The caller must guarantee this upper limit; exceeding it results in undefined hardware behavior at runtime.
- This API is asynchronous. You must wait for the reduction to complete by calling `event_wait(evt, mask)` before reading the result.

## Call Example

```cpp
// For simplicity, the return value reception and error handling are omitted.
using namespace AscendC::ccu;

// Scenario 1: Local on-chip memory -> local on-chip memory reduction (FP16 SUM).
CcuResult my_kernel(ccu_kernel_arg arg) {
    local_addr dst, src;
    variable len;
    event evt;

    // dst must be pre-initialized with an initial value (for example, 0.0).
    local_reduce(dst, src, len, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}

// Scenario 2: Reduce two ccu_buffers to buffers[0], with INT8 input promoted to FP32 output (a valid "input != output" combination;
//        INT8 to FP32 = 4x expansion, so two input channels require reserving four ccu_buffers to hold the expanded output; see the `buffers` parameter description)
CcuResult my_kernel2(ccu_kernel_arg arg) {
    array<ccu_buffer> bufs(4);    // Two input channels; reserve four physically contiguous ccu_buffers according to the expansion ratio
    variable len;
    event evt;

    // bufs[0] and bufs[1] are the two INT8 inputs (initial values must be written in advance);
    // The reduction result is FP32, written starting from buffers[0], occupying four ccu_buffers after expansion
    local_reduce(bufs.data(), 2,
                 HCCL_DATA_TYPE_INT8, HCCL_DATA_TYPE_FP32,
                 HCCL_REDUCE_SUM, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}

// Scenario 3: Regular reduction with the same input and output type (FP16 SUM)
CcuResult my_kernel3(ccu_kernel_arg arg) {
    array<ccu_buffer> bufs(4);
    variable len;
    event evt;

    local_reduce(bufs.data(), 4,
                 HCCL_DATA_TYPE_FP16, HCCL_DATA_TYPE_FP16,
                 HCCL_REDUCE_SUM, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
