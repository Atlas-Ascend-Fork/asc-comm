# CCU_ELSE

<!-- md-trans-meta sourceCommit=2a95f77436441e03223b059000061595597749ee translatedAt=2026-11-04T10:38:58.574Z pushedAt=2026-11-09T06:47:55.014Z nl=2 -->

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

Starts the **else** branch of `CCU_IF` in a CCU kernel: this branch is executed when the `CCU_IF` condition is not met.

`CCU_ELSE` is a preprocessor macro that must be used immediately after `CCU_IF { body }`. It cannot appear independently, and together with `CCU_IF` it forms a complete if-else structure.

> [!NOTE] Note
> `CCU_ELSE` is optional. `CCU_IF` can be used alone, and it is also valid without `CCU_ELSE`.

## Function Prototype

```cpp
CCU_IF(cond_expr) {
    // Body of the then branch.
} CCU_ELSE {
    // Body of the else branch.
}
```

For the complete usage, see [CCU_IF](CCU_IF.md).

## Parameters

`CCU_ELSE` takes no parameters and is automatically paired with the preceding `CCU_IF`, so you do not need to pass any parameters.

## Return Value

`CCU_ELSE` is a preprocessor macro and does not return [`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md) by itself; it does not fail under normal usage.

## Usage Constraints

- `CCU_IF` supports matching only one `CCU_ELSE`. A single `if` with multiple `else` branches is not supported.
- `CCU_ELSE` must immediately follow `CCU_IF { body }`. It cannot appear independently or in any other position.
- No other CCU API calls are allowed between `CCU_IF { body } CCU_ELSE { else-body }`. Once any CCU API call (such as data movement or synchronization) is inserted in between, the framework automatically closes the `CCU_IF` early; the subsequent `CCU_ELSE` then cannot find a matching `CCU_IF`, so its body is skipped while kernel generation still succeeds, and no error value is returned at runtime, making it extremely difficult to debug. Ensure that there are no CCU API calls between `CCU_IF { ... } CCU_ELSE { ... }`.
- The `else if` syntax is not supported. For multiple branches, nest `CCU_IF`:

```cpp
CCU_IF(v == 0) {
    // case 0
} CCU_ELSE {
    CCU_IF(v == 1) {
        // case 1
    } CCU_ELSE {
        // other case
    }
}
```

## Call Example

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    variable mode;
    load_arg(mode, 0);

    CCU_IF(mode == 0) {
        // Handle mode 0.
        variable result;
        result = 100;
    } CCU_ELSE {
        // Handle non-mode-0 cases.
        variable result;
        result = 200;
    }

    return CCU_SUCCESS;
}
```
