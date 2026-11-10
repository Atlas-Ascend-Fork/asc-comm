# local_copy

<!-- md-trans-meta sourceCommit=cf6edf5a4f7bce7570d44b2464954320a3952923 translatedAt=2026-11-04T10:30:07.965Z pushedAt=2026-11-09T06:47:55.008Z nl=2 -->

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

The header file is `#include "ccu/hcomm/ccu_primitives.hpp"`.

Supports asynchronous data copy between local on-chip memory and local CCU Buffer. The data remains unchanged during transfer, and the hardware automatically sets the flag bit corresponding to `mask` in `evt` to 1 when the copy is complete.

The supported data paths are:

- Local on-chip memory -> local on-chip memory
- Local on-chip memory -> local CCU Buffer
- Local CCU Buffer -> local on-chip memory

> [!NOTE] Note
> The automatic flag setting upon movement completion is equivalent to an implicit [event_record](../synchronization/event_record.md), so no explicit call is required.

## Function Prototype

- Local on-chip memory -> local on-chip memory

    ```cpp
    CcuResult local_copy(local_addr dst, local_addr src, variable len,
                        event evt, uint16_t mask = 1);
    ```

- Local on-chip memory -> local CCU Buffer

    ```cpp
    CcuResult local_copy(ccu_buffer dst, local_addr src, variable len,
                        event evt, uint16_t mask = 1);
    ```

- Local CCU Buffer -> local on-chip memory

    ```cpp
    CcuResult local_copy(local_addr dst, ccu_buffer src, variable len,
                        event evt, uint16_t mask = 1);
    ```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| dst | Input | Destination address. Data is written to the memory it points to.<br>&bull;Local on-chip memory -> local on-chip memory, local CCU Buffer -> local on-chip memory: type is `local_addr`.<br>&bull;Local on-chip memory -> local CCU Buffer: type is `ccu_buffer`. |
| src | Input | Source address.<br>&bull;Local on-chip memory -> local on-chip memory, local on-chip memory -> local CCU Buffer: type is `local_addr`.<br>&bull;Local CCU Buffer -> local on-chip memory: type is `ccu_buffer`. |
| len | Input | Number of bytes to copy. The type is `variable`, and the actual value is taken from the corresponding general-purpose register at the dispatch stage. |
| evt | Input | Completion event object. When the hardware movement completes, the flag bit corresponding to `mask` in `evt` is automatically set to 1. The downstream calls `event_wait(evt, mask)` to wait. |
| mask | Input | 16-bit event mask, specifying which flag bits of `event` are set when the hardware completes. The default value is `1`, meaning only **bit0** is set. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation stage. |
| `CCU_E_NOT_FOUND` | The passed `dst`/`src`/`len`/`event` handle is not created in the current kernel. |

## Usage Constraints

- `len` must be greater than 0; otherwise, the hardware exhibits undefined behavior. The caller must guarantee this constraint.
- The address ranges of `dst` and `src` must not overlap; otherwise, the hardware exhibits undefined behavior.
- When the source or destination is `ccu_buffer`, `len` must not exceed the size of a single `ccu_buffer` slice (4096 bytes). The caller must guarantee this upper limit; otherwise, the hardware exhibits undefined behavior.
- This API is asynchronous. You must wait for the movement to complete by calling `event_wait(evt, mask)` before accessing the destination memory.

## Call Example

```cpp
// For simplicity, the return value reception and error handling are omitted.
using namespace AscendC::ccu;

// Scenario 1: local on-chip memory -> local on-chip memory copy
CcuResult my_kernel(ccu_kernel_arg arg) {
    local_addr src, dst;
    variable len;
    event evt;

    local_copy(dst, src, len, evt);    // mask defaults to 0x1.
    event_wait(evt);
    return CCU_SUCCESS;
}

// Scenario 2: local on-chip memory → local CCU Buffer copy
CcuResult my_kernel2(ccu_kernel_arg arg) {
    ccu_buffer buf;
    local_addr src;
    variable len;
    event evt;

    local_copy(buf, src, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}

// Scenario 3: local CCU Buffer → local on-chip memory copy
CcuResult my_kernel3(ccu_kernel_arg arg) {
    local_addr dst;
    ccu_buffer buf;
    variable len;
    event evt;

    local_copy(dst, buf, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
