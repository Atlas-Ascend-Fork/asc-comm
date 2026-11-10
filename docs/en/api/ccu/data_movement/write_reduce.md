# write_reduce

<!-- md-trans-meta sourceCommit=cf6edf5a4f7bce7570d44b2464954320a3952923 translatedAt=2026-11-04T10:37:16.122Z pushedAt=2026-11-09T06:47:55.012Z nl=2 -->

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

Asynchronously writes local data to the remote on-chip memory and, at the same time, reduces it with the existing remote data using the operator specified by `op_type`: `remote = reduce(remote, local, op_type)`. When the hardware completes, the flag bits corresponding to `mask` in `evt` are automatically set to **1**.

The supported data paths are:

- Local on-chip memory -> remote on-chip memory

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {
CcuResult write_reduce(ChannelHandle ch, remote_addr remote, local_addr local,
                      variable len, HcclDataType data_type, HcclReduceOp op_type,
                      event evt, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| ch | Input | Cross-rank channel handle. |
| remote | Input | Destination address in the remote on-chip memory. The remote memory must contain a valid initial value before the call, which is the responsibility of the remote kernel. After the hardware completes, the content at this address is updated to the reduction result. |
| local | Input | Source address in the local on-chip memory. |
| len | Input | Number of bytes to operate on, of type `variable`. The actual value is taken from the corresponding general-purpose register at the dispatch stage. |
| data_type | Input | Data type. See the `HcclDataType` enum for values. |
| op_type | Input | Reduction operator. See the `HcclReduceOp` enum for values. When SUM is used, the sum of low-precision input data is first promoted to a higher precision and then adjusted back to the same precision as the input data. |
| evt | Input | Completion event object. When the hardware reduction write completes, the flag bits corresponding to `mask` in `evt` are automatically set to 1, and the downstream call `event_wait(evt, mask)` waits on them. |
| mask | Input | 16-bit event mask, specifying which flag bits of `event` are set when the hardware completes. The default value is `1`, meaning that only **bit0** is set. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation stage. |
| `CCU_E_NOT_FOUND` | The passed `remote`/`local`/`len`/`event` handle is not created in the current kernel. |

## Usage Constraints

- The `remote` memory must contain a valid initial value before the call; otherwise, the reduction result is undefined.
- The parameter order is destination-first, source-second: `write_reduce(ch, remote, local, ...)`. Reversing the order causes a compile-time error because the types differ.
- `len` must be greater than 0; otherwise, the hardware exhibits undefined behavior. The caller must ensure this constraint.
- `len` must be less than 256 MB. The caller must ensure this upper limit; if it is exceeded, the hardware exhibits undefined behavior. Overlong data must be split into multiple transfers.
- All channels within the same kernel must belong to the same die. This constraint is uniformly checked by `<<<>>>`, and `CCU_E_PARA` is returned when it is violated.
- `data_type` supports only `HCCL_DATA_TYPE_INT8`, `HCCL_DATA_TYPE_UINT8`, `HCCL_DATA_TYPE_INT16`, `HCCL_DATA_TYPE_UINT16`, `HCCL_DATA_TYPE_INT32`, `HCCL_DATA_TYPE_UINT32`, `HCCL_DATA_TYPE_FP16`, `HCCL_DATA_TYPE_BFP16`, and `HCCL_DATA_TYPE_FP32`.
- `op_type` supports only `HCCL_REDUCE_SUM`, `HCCL_REDUCE_MAX`, and `HCCL_REDUCE_MIN`.
- When `data_type`/`op_type` is outside the supported range, an exception is thrown (carrying an error code), which is uniformly caught by `<<<>>>`.
- This API is asynchronous. You must call `event_wait(evt, mask)` to wait for the reduction to complete, so that the remote data is guaranteed to be visible.

## Call Example

```cpp
// For simplicity, the return value reception and error handling are omitted.
using namespace AscendC::ccu;

// User-defined kernel argument structure, passed through kernelArg of <<<>>> after the link is established on the host side.
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// Scenario: Write local FP16 data to the remote end and perform SUM reduction.
CcuResult my_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*. Cast it to the user input parameter struct first.
    ChannelHandle ch = params->channel_handle;
    remote_addr remote;    // The remote end must write the initial value in advance.
    local_addr src;
    variable len;
    event evt;

    write_reduce(ch, remote, src, len, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
