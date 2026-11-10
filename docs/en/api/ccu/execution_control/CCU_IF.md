# CCU_IF

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:40:47.184Z pushedAt=2026-11-09T06:47:55.015Z nl=2 -->

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

Starts a software conditional branch block inside a CCU kernel. The condition is computed by the CCU hardware during the dispatch phase. When the condition is met, the then code block is executed; otherwise, execution jumps to the exit of the **if** block.

## Function Prototype

```cpp
CCU_IF(cond_expr) {
    // then branch code block
}
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| cond_expr | Input | Conditional expression of type `AscendC::ccu::cond_expr`. It is produced by `operator==(uint64_t)` or `operator!=(uint64_t)` of `ccu::variable`, for example, `v == 0` or `counter != 10`. |

## Return Value

`CCU_IF` is a preprocessor macro and does not return [`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md) by itself. After macro expansion, if an internal error occurs (such as a duplicate label or an invalid handle), `CCU_E_PARA`/`CCU_E_NOT_FOUND` may be produced. In this case, the macro does not throw an exception and does not block kernel generation: on failure, the code block is skipped and kernel generation still succeeds. Errors are not reported through a return value, so pay attention to them during debugging.

## Usage Constraints

- `CCU_IF(cond_expr)` must be immediately followed by a then code block enclosed in `{}`.
- `cond_expr` supports only the `==` and `!=` comparison operations. `<`, `>`, `<=`, and `>=` are not supported on any product.
- Nesting is supported: `CCU_IF` can be nested inside a `CCU_IF` code block.
- It cannot be used inside the loop body of a hardware loop. Although no error is reported, the hardware exhibits undefined behavior.
- One side of the comparison in `cond_expr` must be a `uint64_t` immediate value. For Ascend 950PR&950DT products, comparison between `variable` and `variable` is not supported.

## Call Example

```cpp
using namespace AscendC::ccu;

// Scenario: Select different execution paths based on the value of the stage variable.
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable flag;
    load_arg(flag, 0);   // Load the flag value from taskArgs[0] on each dispatch.

    CCU_IF(flag == 0) {
        // Execute this branch when flag is 0.
        variable a, b;
        a = 10;
        b = 20;
    }

    return CCU_SUCCESS;
}
```
