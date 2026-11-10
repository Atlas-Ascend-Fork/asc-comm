# store

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:28:44.730Z pushedAt=2026-11-09T06:47:55.007Z nl=2 -->

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

The header file is `#include "ccu/hcomm/ccu_primitives.hpp"`

Writes the `uint64_t` value of one or more variables to an on-chip memory address in a CCU kernel.

It is the reverse operation of `load`. The supported data paths are as follows:

- Single variable -> immediate address
- Variable array -> immediate address
- Single variable -> variable address (indirect addressing)
- Variable array -> variable address (indirect addressing)

The immediate address is determined at compile time and is immutable during the delivery phase; the variable address is read from a hardware register at runtime.

## Function Prototype

- Single variable -> immediate address

    ```cpp
    CcuResult store(uint64_t addr, variable v);
    ```

- variable array -> immediate address

    ```cpp
    CcuResult store(uint64_t addr, array<variable>& v_arr, uint32_t num);
    ```

- single variable -> variable address (indirect addressing)

    ```cpp
    CcuResult store(variable addr_var, variable v);
    ```

- variable array -> variable address (indirect addressing)

    ```cpp
    CcuResult store(variable addr_var, array<variable>& v_arr, uint32_t num);
    ```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| addr | Input | Immediate on-chip memory target address (`uint64_t`, immediate address path). It must be a physical address accessible by the CCU or a tokenized VA, determined at compile time and immutable during the delivery phase. |
| addr_var | Input | Address variable (`variable`, variable address path). At runtime, the value stored in this variable is used as the on-chip memory target address, and it must have been assigned a valid address value. |
| v | Input | Source variable (single variable path). At runtime, the value of this variable is written to the on-chip memory target address. |
| v_arr | Input | First element of the source variable array (variable array path). It must be allocated through `ccu::array<variable>` to guarantee physical contiguity. |
| num | Input | Number of `uint64_t` elements to store (variable array path). It must satisfy `1 ≤ num ≤ v_arr.size()`; when `num>1`, `v_arr[0]` through `v_arr[num-1]` are written sequentially to on-chip memory locations starting from the start address with an 8-byte stride. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PARA` | Parameter error: `num` is **0** or greater than `v_arr.size()`; or when `num>1`, the elements of `v_arr` are physically non-contiguous. |
| `CCU_E_PTR` | The API is called outside the compile phase. |
| `CCU_E_NOT_FOUND` | The passed `v`/`v_arr` handle (or the adjacent handles of `v_arr[1..num-1]`) is not created in the current kernel. |

## Usage Constraints

- `addr` must be a physical address accessible to the CCU or a tokenized VA. Directly passing a non-tokenized process virtual address will trigger a driver error.
- `num` must satisfy `1 ≤ num ≤ v_arr.size()`. This API performs this check before accessing `v_arr[0]`; passing 0 or a value exceeding `v_arr.size()` returns `CCU_E_PARA` without reaching the underlying API. For an empty `array` (where `v_arr.size()` is 0), passing any `num` also returns `CCU_E_PARA`.
- When `num>1` (variable array path), `v_arr` must point to a physically contiguous array of `variable` objects, which must be allocated through `ccu::array<variable>`. Declaring multiple `variable` objects separately does not guarantee physical contiguity. If this requirement is violated, `CCU_E_PARA` is returned at the `store(...)` call site.
- `addr_var` (variable address path) must have been assigned a valid address value (through `load_arg`, immediate assignment, or arithmetic operations) before this API is called.
- The immediate address path is determined at compile time and immutable during the delivery phase; use the variable address path when a runtime dynamic address is required.

## Call Example

```cpp
// For simplicity, return value reception and error handling are omitted.
using namespace AscendC::ccu;

// Scenario 1: Store one variable to an immediate address (single variable -> immediate address).
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable result;
    // ... Compute result ...
    store(0x20000000ULL, result);
    return CCU_SUCCESS;
}

// Scenario 2: Store four variables in batch to an immediate address (variable array -> immediate address).
CcuResult my_kernel2(ccu_kernel_arg arg) {
    array<variable> v_arr(4);    // Four physically contiguous variables.
    // ... Compute v_arr ...
    store(0x80000000ULL, v_arr, 4);
    return CCU_SUCCESS;
}

// Scenario 3: Store a variable to the address specified by a variable (single variable -> variable address).
CcuResult my_kernel3(ccu_kernel_arg arg) {
    variable dst_addr, result;
    load_arg(dst_addr, 0);    // The host passes in the destination address.
    // ... Compute result ...
    store(dst_addr, result);  // Write through indirect addressing at runtime.
    return CCU_SUCCESS;
}
```
