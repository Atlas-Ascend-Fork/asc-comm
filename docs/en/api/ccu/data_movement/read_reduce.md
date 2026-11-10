# read_reduce

<!-- md-trans-meta sourceCommit=cf6edf5a4f7bce7570d44b2464954320a3952923 translatedAt=2026-11-04T10:32:52.900Z pushedAt=2026-11-09T06:47:55.009Z nl=2 -->

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

Asynchronously reads data from the remote on-chip memory through the established [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md) and merges it with the existing data in the local on-chip memory using the specified operator (`local = reduce(local, fetch_from_remote, op_type)`). When the hardware completes, the flag bit corresponding to `mask` in `evt` is automatically set to **1**.

## Function Prototype

```cpp
CcuResult read_reduce(ChannelHandle ch, local_addr local, remote_addr remote,
                      variable len, HcclDataType data_type, HcclReduceOp op_type,
                      event evt, uint16_t mask = 1);
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| ch | Input | Cross-rank channel handle. |
| local | Input/Output | Destination address in the local on-chip memory, of type `local_addr`. It is also the accumulation side of the reduction. A valid initial value must be written before the call; after the hardware completes, it is updated to the reduction result. |
| remote | Input | Source address on the remote side, of type `remote_addr`. |
| len | Input | Number of bytes to operate on, of type `variable`. The actual value is taken from the corresponding general-purpose register at the dispatch stage. |
| data_type | Input | Data type. For values, see the `HcclDataType` enumeration. Only the following six types are supported: `HCCL_DATA_TYPE_UINT8`, `HCCL_DATA_TYPE_INT16`, `HCCL_DATA_TYPE_INT32`, `HCCL_DATA_TYPE_FP16`, `HCCL_DATA_TYPE_FP32`, and `HCCL_DATA_TYPE_BFP16`. Other values are rejected and an exception is thrown (with an error code). |
| op_type | Input | Reduction operator. For values, see the `HcclReduceOp` enumeration. Only `HCCL_REDUCE_SUM` (sum), `HCCL_REDUCE_MAX` (maximum), and `HCCL_REDUCE_MIN` (minimum) are supported. `HCCL_REDUCE_PROD` is not supported; passing it is rejected and an exception is thrown (with an error code). When the SUM operation is used, the sum result of low-precision input data is first promoted to a higher precision and then adjusted back to the same precision as the input data. |
| evt | Input | Completion event object. When the hardware reduction read completes, the flag bit corresponding to `mask` in `evt` is automatically set to **1**. The downstream code waits by calling `event_wait(evt, mask)`. |
| mask | Input | 16-bit event mask that specifies which flag bits of `event` are set when the hardware completes. The default value is `1`, which sets only **bit0**. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API was called outside the compilation stage. |
| `CCU_E_NOT_FOUND` | The passed `local`/`remote`/`len`/`event` handle was not created in the current kernel. |

## Usage Constraints

- The parameter order is destination first and source second: `read_reduce(ch, local, remote, ...)`, where `local` is also the accumulation side of the reduction.
- All [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md) objects in the same kernel must belong to the same die. Channels from different dies cannot be mixed in the same kernel. Die consistency is uniformly checked by `<<<>>>`; if inconsistent, `CCU_E_PARA` is returned instead of the return value of this API.
- `local` must have a valid initial value (such as 0 or negative infinity) written before the call; otherwise, the reduction result is undefined.
- This API is asynchronous. You must wait for the reduction to complete by calling `event_wait(evt, mask)` before reading the result.

## Call Example

```cpp
// For simplicity, the return value reception and error handling are omitted.
using namespace AscendC::ccu;

// User-defined kernel argument struct. After the link is established on the host, it is passed in through the kernelArg of <<<>>>.
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// Scenario: Read from the peer on-chip memory and perform FP16 SUM reduction with the local data.
CcuResult my_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*, so first cast it to the user argument struct.
    ChannelHandle ch = params->channel_handle;
    local_addr dst;      // dst must be pre-initialized with an initial value (for example, 0.0).
    remote_addr remote;
    variable len;
    event evt;

    read_reduce(ch, dst, remote, len, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
