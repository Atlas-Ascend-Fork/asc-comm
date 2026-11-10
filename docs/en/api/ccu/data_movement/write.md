# write

<!-- md-trans-meta sourceCommit=cf6edf5a4f7bce7570d44b2464954320a3952923 translatedAt=2026-11-04T10:36:18.650Z pushedAt=2026-11-09T06:47:55.011Z nl=2 -->

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

Asynchronously writes local data to the remote on-chip memory through the established [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md). When the hardware completes the operation, it automatically sets the flag bit corresponding to `mask` in `evt` to **1**.

The supported data paths are:

- Local on-chip memory -> remote on-chip memory
- Local CCU Buffer -> remote on-chip memory

## Function Prototype

- Local on-chip memory -> remote on-chip memory

    ```cpp
    CcuResult write(ChannelHandle ch, remote_addr remote, local_addr local,
                    variable len, event evt, uint16_t mask = 1);
    ```

- Local CCU Buffer -> remote on-chip memory

    ```cpp
    CcuResult write(ChannelHandle ch, remote_addr remote, ccu_buffer local,
                    variable len, event evt, uint16_t mask = 1);
    ```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| ch | Input | Cross-rank channel handle. |
| remote | Input | Destination address, whose type is `remote_addr`. |
| local | Input | Source address.<br>&bull;Local on-chip memory -> remote on-chip memory: type is `local_addr`.<br>&bull;Local CCU Buffer -> remote on-chip memory: type is `ccu_buffer`. |
| len | Input | Number of bytes to write, whose type is `variable`. The actual value is obtained from the corresponding general-purpose register at the dispatch stage. |
| evt | Input | Completion event object. When the hardware completes the write, it automatically sets the flag bit corresponding to `mask` in `evt` to 1. Downstream calls `event_wait(evt, mask)` to wait. |
| mask | Input | 16-bit event mask, specifying which flag bits of `event` are set when the hardware completes. The default value is `1`, which sets only **bit0**. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation phase. |
| `CCU_E_NOT_FOUND` | The passed `remote`/`local`/`len`/`event` handle is not created in the current kernel. |

## Usage Constraints

- The parameter order is destination first and source second: `write(ch, remote, local, ...)`. Reversing the order causes a compile-time error because the types differ.
- `len` must be greater than 0; otherwise, the hardware exhibits undefined behavior. This constraint must be ensured by the caller.
- For "local on-chip memory -> remote on-chip memory", `len` must be less than 256 MB. This upper limit must be ensured by the caller; if exceeded, the hardware exhibits undefined behavior.
- For "local CCU Buffer -> remote on-chip memory", `len` must not exceed the size of a single `ccu_buffer` slice (4096 bytes). This upper limit must be ensured by the caller; if exceeded, the hardware exhibits undefined behavior.
- All [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md) objects in the same kernel must belong to the same die. Channels from different dies cannot be mixed in the same kernel. Die consistency is uniformly verified by `<<<>>>`; if inconsistent, `CCU_E_PARA` is returned instead of the return value of this API.
- This API is asynchronous. You must call `event_wait(evt, mask)` to wait for the write to complete before the remote data is guaranteed to be visible.

## Call Example

```cpp
// For simplicity, the return value reception and error handling are omitted.
using namespace AscendC::ccu;

// User-defined kernel input parameter struct, passed through the kernelArg of <<<>>> after the host side establishes the link.
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// Scenario 1: local on-chip memory -> remote on-chip memory
CcuResult my_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*, so first cast it to the user input parameter struct.
    ChannelHandle ch = params->channel_handle;
    remote_addr remote;
    local_addr src;
    variable len;
    event evt;

    write(ch, remote, src, len, evt);    // remote comes first, local comes last.
    event_wait(evt);
    return CCU_SUCCESS;
}

// Scenario 2: local CCU buffer -> remote on-chip memory
CcuResult my_kernel2(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*, so first cast it to the user input parameter struct.
    ChannelHandle ch = params->channel_handle;
    remote_addr remote;
    ccu_buffer buf;
    variable len;
    event evt;

    write(ch, remote, buf, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
