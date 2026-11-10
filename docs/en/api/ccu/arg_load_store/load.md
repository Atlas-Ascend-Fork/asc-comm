# load

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:27:35.389Z pushedAt=2026-11-09T06:47:55.005Z nl=2 -->

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

Reads one or more `uint64_t` values from an on-chip memory address and writes them to a variable within a CCU kernel.

The supported address paths are:

- Immediate address -> single variable
- Immediate address -> variable array
- Variable address (indirect addressing) -> single variable
- Variable address (indirect addressing) -> variable array

The immediate address is determined at compile time and is immutable during the delivery phase; the variable address is read from hardware registers at runtime.

## Function Prototype

- Immediate address -> single variable

    ```cpp
    CcuResult load(uint64_t addr, variable v);
    ```

- Immediate address -> variable array

    ```cpp
    CcuResult load(uint64_t addr, array<variable>& v_arr, uint32_t num);
    ```

- Variable address (indirect addressing) -> single variable

    ```cpp
    CcuResult load(variable addr_var, variable v);
    ```

- Variable address (indirect addressing) -> variable array

    ```cpp
    CcuResult load(variable addr_var, array<variable>& v_arr, uint32_t num);
    ```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| addr | Input | Immediate on-chip memory address (`uint64_t`, immediate address path). It must be a physical address accessible to the CCU or a tokenized VA, determined at compile time and immutable during the delivery phase. |
| addr_var | Input | Address variable (`variable`, variable address path). At runtime, the value stored in this variable is used as the on-chip memory address, and it must have been assigned a valid address value. |
| v | Input/Output | Target variable (single variable path). At runtime, reads 8 bytes from the on-chip memory address and writes them into this variable. |
| v_arr | Input/Output | First element of the target variable array (variable array path). It must be allocated through `ccu::array<variable>` to guarantee physical contiguity. |
| num | Input | Number of `uint64_t` elements to load (variable array path). It must satisfy `1 ≤ num ≤ v_arr.size()`; when `num>1`, reads sequentially from the start address with an 8-byte stride and writes into `v_arr[0]` through `v_arr[num-1]`. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PARA` | Parameter error: `num` is 0 or greater than `v_arr.size()`; or when `num>1`, the elements of `v_arr` are physically non-contiguous. |
| `CCU_E_PTR` | The API is called outside the compile phase. |
| `CCU_E_NOT_FOUND` | The passed `v`/`v_arr` handle (or the adjacent handles of `v_arr[1..num-1]`) is not created in the current kernel. |

## Usage Constraints

- `addr` must be a physical address accessible to the CCU or a tokenized VA. Directly passing a non-tokenized process virtual address will trigger a driver error.
- `num` must satisfy `1 ≤ num ≤ v_arr.size()`. This API performs this check before accessing `v_arr[0]`; passing **0** or a value exceeding `v_arr.size()` returns `CCU_E_PARA` without reaching the underlying API. For an empty `array` (where `v_arr.size()` is 0), passing any `num` also returns `CCU_E_PARA`.
- When `num>1` (variable array path), `v_arr` must point to a physically contiguous array of `variable` objects, which must be allocated through `ccu::array<variable>`. Declaring multiple `variable` objects separately does not guarantee physical contiguity. If this requirement is violated, `CCU_E_PARA` is returned at the `load(...)` call site.
- `addr_var` (variable address path) must have been assigned a valid address value (through `load_arg`, immediate assignment, or arithmetic operations) before this API is called.
- The immediate address path is determined at compile time and immutable during the delivery phase; use the variable address path when a runtime dynamic address is required.

## Call Example

```cpp
// For simplicity, return value reception and error handling are omitted.
using namespace AscendC::ccu;

// Scenario 1: Load one variable from an immediate address (immediate address -> single variable).
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable v;
    load(0x10000000ULL, v);
    return CCU_SUCCESS;
}

// Scenario 2: Load four variables in batch from an immediate address (immediate address -> variable array).
CcuResult my_kernel2(ccu_kernel_arg arg) {
    array<variable> v_arr(4);    // Four physically contiguous variables.
    load(0x80000000ULL, v_arr, 4);
    return CCU_SUCCESS;
}

// Scenario 3: Load indirectly from a variable address (variable address -> single variable, where the address is computed in the previous step).
CcuResult my_kernel3(ccu_kernel_arg arg) {
    variable src_addr, v;
    load_arg(src_addr, 0);    // Address passed in by the host.
    load(src_addr, v);        // Indirect addressing at runtime.
    return CCU_SUCCESS;
}
```
