# CCU_DO

<!-- md-trans-meta sourceCommit=2a95f77436441e03223b059000061595597749ee translatedAt=2026-11-04T10:38:53.590Z pushedAt=2026-11-09T06:47:55.014Z nl=2 -->

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

Starts a do-while loop block in a CCU kernel: the body is executed at least once, and then the accompanying `CCU_WHILE` determines whether to continue.

`CCU_DO` is a preprocessor macro that must be terminated with [CCU_WHILE](CCU_WHILE.md) to form a complete do-while loop:

```cpp
CCU_DO { body } CCU_WHILE(cond_expr);
```

> [!CAUTION] Caution
> `CCU_DO` must be terminated with `CCU_WHILE(cond);`. Omitting the termination does not cause a compilation error, but leads to incorrect runtime behavior: the body is executed only once and then execution proceeds sequentially downward, and it affects subsequent `CCU_WHILE` statements, causing disordered loop jumps that are extremely difficult to debug.

## Function Prototype

```cpp
CCU_DO {
    // body (executed at least once)
} CCU_WHILE(cond_expr);
```

## Parameters

`CCU_DO` takes no parameters. For the definition and generation of `cond_expr` in `CCU_WHILE(cond_expr)`, see [CCU_IF](CCU_IF.md#parameters).

## Return Value

`CCU_DO` is a preprocessor macro and does not return [`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md) itself; it does not fail under normal usage.

## Usage Constraints

- `CCU_DO` must be immediately terminated by `CCU_WHILE(cond);`. If the terminator is missing, the runtime behavior is incorrect, and no compile-time error is generated, nor does kernel generation fail.
- No other CCU API calls are allowed between `CCU_DO { body }` and its matching `CCU_WHILE(cond);`; otherwise, the do-while loop cannot be paired correctly.
- `CCU_DO ... CCU_WHILE` cannot be used inside the body lambda of `ccu::loop`, because software loops are not supported inside a hardware loop body.
- One side of the comparison in `cond_expr` must be a `uint64_t` immediate value. For Ascend 950PR&950DT products, comparison between `variable` and `variable` is not supported.

## Call Example

```cpp
using namespace AscendC::ccu;

// Scenario: do-while loop, where the body is executed at least once and then the condition determines whether to continue.
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable inner, one;
    inner = 0;
    one = 1;

    // do-while: the body is executed once first, and then the condition is evaluated (the condition supports only comparison between a variable and a uint64 immediate value).
    CCU_DO {
        inner = inner + one;
    } CCU_WHILE(inner != 5ULL);

    return CCU_SUCCESS;
}
```
