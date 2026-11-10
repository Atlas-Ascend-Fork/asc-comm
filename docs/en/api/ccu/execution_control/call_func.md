# call_func

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:38:11.676Z pushedAt=2026-11-09T06:47:55.013Z nl=2 -->

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

This API is used for calling function blocks. It is used together with [func](func.md). On the first call, `call_func<obj>(args...)` executes the lambda of `obj`, synthesizes the logic into a function block, and appends a jump instruction. Subsequent calls to the same `obj` only append a jump instruction and reuse the synthesized function block. For how to define a function block, see [func](func.md).

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {

// obj must be a ccu::func object in namespace scope or with static storage, and each argument in args must be a ccu::variable.
template <func &obj, typename... args>
CcuResult call_func(args... args_);

} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| obj | Input | Template parameter, the `ccu::func` object to be called, which must be an object in namespace scope or with static storage. |
| args... | Input | Argument list. The number of arguments must match the number of lambda parameters of `obj`. Each argument is of type `ccu::variable` and is bound to the corresponding lambda parameter when called. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PARA` | The number of arguments does not match the number of lambda parameters of `obj`. |
| Others | The corresponding error code returned when the jump instruction fails to be issued. |

> [!NOTE] Note
> Other failure cases throw an exception (carrying an error code) instead of being reported through the return value, such as calling inside a hardware loop body, nested calls, and calling outside the compilation phase. For details, see [Usage Constraints](#usage-constraints).

## Usage Constraints

- `obj` must be a `ccu::func` object in namespace scope or with static storage, and cannot be a function-local variable: `call_func` uses a `func` object reference as the template parameter, which is a C++ language requirement. Violating this causes a compile-time error.
- Do not call it inside a hardware loop body; otherwise, an exception is thrown (error code `CCU_E_INTERNAL`).
- Nested calls are not allowed: `call_func` cannot be called again inside a lambda. Otherwise, an exception is thrown (error code `CCU_E_INTERNAL`).
- The function block is synthesized on the first call, and subsequent calls to the same `obj` reuse the existing function block. Different `ccu::func` objects synthesize independent function blocks even if their lambda content is identical.
- Thrown exceptions are uniformly caught by `<<<>>>`, which returns `CCU_E_INTERNAL` to the caller. The caller does not need to catch them.

## Call Example

```cpp
using namespace AscendC::ccu;

// Declare a static object at namespace scope to satisfy the linkage requirement of the template parameter.
static func add_dbl([](variable x) {
    variable tmp;
    tmp = x + x;   // Double x.
});

CcuResult my_kernel(ccu_kernel_arg arg) {
    variable a, b;
    a = 2;
    b = 5;

    // First call: synthesize the function block and append a jump instruction.
    CcuResult ret = call_func<add_dbl>(a);
    if (ret != CCU_SUCCESS) { return ret; }

    // Second call: append only a jump instruction and reuse the synthesized function block.
    return call_func<add_dbl>(b);
}
```
