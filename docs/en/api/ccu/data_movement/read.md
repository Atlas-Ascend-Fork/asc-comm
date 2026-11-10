# read

<!-- md-trans-meta sourceCommit=cf6edf5a4f7bce7570d44b2464954320a3952923 translatedAt=2026-11-04T10:33:07.370Z pushedAt=2026-11-09T06:47:55.010Z nl=2 -->

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

Asynchronously reads data from the peer on-chip memory to the local side through the established [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md). When the hardware completes the operation, it automatically sets the flag bit corresponding to `mask` in `evt` to **1**.

The supported data paths are:

- Remote on-chip memory -> local on-chip memory
- Remote on-chip memory -> local CCU Buffer

## Function Prototype

- Remote on-chip memory -> local on-chip memory

    ```cpp
    CcuResult read(ChannelHandle ch, local_addr local, remote_addr remote,
                   variable len, event evt, uint16_t mask = 1);
    ```

- Remote on-chip memory -> local CCU Buffer

    ```cpp
    CcuResult read(ChannelHandle ch, ccu_buffer local, remote_addr remote,
                   variable len, event evt, uint16_t mask = 1);
    ```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| ch | Input | Cross-rank channel handle. |
| local | Input/Output | Local destination address.<br>&bull;Remote on-chip memory -> local on-chip memory: type is `local_addr`.<br>&bull;Remote on-chip memory -> local CCU Buffer: type is `ccu_buffer`, with a maximum of 4096 bytes per slice. |
| remote | Input | Remote source address, type is `remote_addr`. |
| len | Input | Number of bytes to read, type is `variable`. The actual value is taken from the corresponding general-purpose register at the dispatch stage. For "remote on-chip memory -> local CCU Buffer", it must not exceed 4096 bytes. |
| evt | Input | Completion event object. When the hardware completes the read, it automatically sets the flag bit corresponding to `mask` in `evt` to **1**. Downstream calls `event_wait(evt, mask)` to wait. |
| mask | Input | 16-bit event mask that specifies which flag bits of `event` are set when the hardware completes. The default value is `1`, which sets only **bit0**. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation phase. |
| `CCU_E_NOT_FOUND` | The passed `local`/`remote`/`len`/`event` handle is not created in the current kernel. |

## Usage Constraints

- The parameter order is destination first and source last: `read(ch, local, remote, ...)`, which is the opposite of `write`. Reversing the order causes a compile-time error due to type mismatch.
- All [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md) objects in the same kernel must belong to the same die. Channels from different dies cannot be mixed in the same kernel. Die consistency is uniformly checked by `<<<>>>`, which returns `CCU_E_PARA` on mismatch instead of the return value of this API.
- For "remote on-chip memory -> local CCU Buffer", `len` must not exceed the size of a single `ccu_buffer` slice (4096 bytes). The caller must ensure this limit; exceeding it results in undefined runtime hardware behavior.
- This API is an asynchronous operation. You must wait for the read to complete by calling `event_wait(evt, mask)` before accessing the target memory.

## Call Example

```cpp
// For simplicity, the return value reception and error handling are omitted.
using namespace AscendC::ccu;

// User-defined kernel argument struct, passed through the kernelArg of <<<>>> after the host side establishes the channel.
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// Scenario 1: Read from remote on-chip memory to local on-chip memory.
CcuResult my_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*. Cast it to the user argument struct first.
    ChannelHandle ch = params->channel_handle;
    local_addr dst;
    remote_addr remote;
    variable len;
    event evt;

    read(ch, dst, remote, len, evt);    // local first, remote last.
    event_wait(evt);
    return CCU_SUCCESS;
}

// Scenario 2: Read from remote on-chip memory to the local CCU Buffer.
CcuResult my_kernel2(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*. Cast it to the user input parameter struct first.
    ChannelHandle ch = params->channel_handle;
    ccu_buffer buf;
    remote_addr remote;
    variable len;
    event evt;

    read(ch, buf, remote, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
