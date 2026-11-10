# load_arg

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:27:38.870Z pushedAt=2026-11-09T06:47:55.004Z nl=2 -->

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

`load_arg` is called inside the kernel function body to load parameters passed from the host into a `variable`. Each time the host dispatches a kernel through `<<<>>>`, it passes parameters through the `taskArgs` array. Before the kernel starts executing, the hardware writes the value of `taskArgs[arg_id]` into the register corresponding to the target `variable` as its initial value.

This API only records the declaration at compile time and does not perform the load. The actual load is performed by the hardware on each dispatch. Therefore, the same compiled kernel can carry different parameters on each dispatch without recompilation.

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {
CcuResult load_arg(variable v, uint32_t arg_id);
} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| v | Input | Target `variable` object that receives the value injected from `taskArgs[arg_id]` as its initial value. |
| arg_id | Input | Index into the `taskArgs` array. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API was called outside the compile phase. |
| `CCU_E_NOT_FOUND` | The `v` handle passed in was not created in the current kernel. |

> [!NOTE] Note
> This API does not return `CCU_E_PARA`. Constraint errors related to `arg_id` are not returned by this API. Instead, they are uniformly validated by `<<<>>>`, which returns `CCU_E_INTERNAL`. For details, see [Usage Constraints](#usage-constraints).

## Usage Constraints

- Within the same kernel, the `arg_id` values of all `load_arg` calls must be numbered consecutively starting from 0 (0, 1, 2, ...), must not be duplicated, and the total count must be exactly equal to the `argNum` of `<<<>>>`. Load instructions are mapped to task parameter slots in ascending order of `arg_id`. Skipped, out-of-order, or duplicated numbers prevent parameters from being correctly mapped to slots. All of the above are uniformly validated by `<<<>>>`, which returns `CCU_E_INTERNAL` on violation.
- `argNum` represents the number of elements, not the number of bytes. For example, for three `load_arg` calls with `arg_id` values 0, 1, and 2, `argNum` must be `3`, and `taskArgs` must contain at least three `uint64_t` elements.
- `arg_id` ranges from 0 to 12, because a CCU task descriptor carries at most 13 parameters.
- `load_arg` only sets the initial value of `v`: if the kernel function body assigns a value to the same `v` again, the assignment overwrites the injected initial value. When mixing the two, read out the injected value first and then reassign it.

## Call Example

```cpp
// For simplicity, the return value handling and error handling are omitted.
using namespace AscendC::ccu;

// Scenario: the loop count n and the start offset are declared at compile time and passed by the host at launch.
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable n, offset;

    load_arg(n, 0);        // taskArgs[0] → n on each dispatch
    load_arg(offset, 1);   // taskArgs[1] → offset on each dispatch

    // Use n and offset for the loop and address calculation later.
    // ...
    return CCU_SUCCESS;
}

// Corresponding <<<>>> on the host side (illustrative):
// uint64_t taskArgs[] = {100, 4096};   // n=100 iterations, offset=4096 bytes
// kernel_name<<<schd, insHandle, stream>>>(taskArgs[0], taskArgs[1]);
```
