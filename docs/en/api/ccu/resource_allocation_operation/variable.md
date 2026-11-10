# variable

<!-- md-trans-meta sourceCommit=5f8b41dd145ea0a95acdd2f2f2ea4995d681fdd0 translatedAt=2026-11-05T01:39:22.031Z pushedAt=2026-11-09T06:49:22.785Z nl=2 -->

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

Header file: `#include "ccu/hcomm/ccu_primitives.hpp"`.

`ccu::variable` is a C++ wrapper class for general-purpose registers in a CCU kernel. It stores scalar values and supports assignment, addition, and comparison operations used by control-flow macros such as `CCU_IF`/`CCU_WHILE`/`CCU_DO`.

- Allocation upon construction: the default constructor only requests a virtual handle.
-No release upon destruction: Hardware resources are not released upon destruction. Physical resources are managed uniformly over the CCU instance lifecycle, and the virtual handle becomes invalid after translation.
- Operators for device operations: assignment and arithmetic operators describe operations executed on the device side. They operate on the corresponding registers during the dispatch phase, rather than being computed immediately on the host side.

> [!NOTE] Note
> Resource allocation uses a two-phase "virtual first, physical later" model: the `variable()` constructor only produces a virtual handle, and the actual physical scalar registers are allocated uniformly during the compile phase.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {
class variable final {
public:
    // Default constructor: request a virtual handle for one scalar register.
    variable();
    // Reserved constructor: bind to the index-th scalar register reserved on the host side, without requesting a new scalar register.
    explicit variable(ccu_variable_handle var_handle, uint32_t index = 0);

    // Assignment operator.
    void operator=(uint64_t immediate) const;                    // Assign an immediate value.
    void operator=(const variable& other) const;                 // Assign between variable objects.

    // Arithmetic operators
    void operator+=(const variable& other) const;                // In-place addition.
    /* Internal expression object */ operator+(const variable& that) const;     // Addition (expression template).

    // Conditional operators
    cond_expr operator==(uint64_t immediate);                     // Produce cond_expr.
    cond_expr operator!=(uint64_t immediate);                     // Produce cond_expr.

    ccu_variable_handle handle{0};                                // Virtual handle.
};
} // namespace ccu
} // namespace AscendC
```

## Constructor Description

- Default constructor

    Use `variable v;` to request one scalar register virtual handle.

- Reserved constructor

    The parameters of the reserved constructor `variable v(var_handle, index);` are as follows:

    | Parameter | Input/Output | Description |
    | --- | --- | --- |
    | var_handle | Input | Reserved handle, which is returned after the host side calls `asccomm_ccu_variable_alloc` to pre-allocate a segment of physical scalar registers from the instance resource pool, and is passed into the kernel through `kernelArg`. |
    | index | Input | Sequence number of the scalar register to be bound within the reserved segment. The default value is `0`, and the value range is `[0, num-1]` (`num` is the number of scalar registers reserved by `asccomm_ccu_variable_alloc` on the host side). If the value is out of range, a `CCU_E_PARA` exception is thrown. |

    Constructing multiple `variable` objects with the same reserved handle and the same `index` makes them share the same physical scalar register.

## Operator Description

### Assignment Operators

| Expression Syntax | Hardware Semantics |
| --- | --- |
| `v = imm;` (`imm` is `uint64_t`) | `v ← imm`. The immediate value is determined at compile phase and cannot be changed at the dispatch phase. |
| `d = s;` (`s` is a `variable`) | `d ← s`. A device-side register assignment, not a host-side handle copy. |

### Arithmetic Operators

| Expression Syntax | Hardware Semantics |
| --- | --- |
| `r = a + b;` | `r ← a + b` (a single dual-source addition instruction). `operator+` returns an expression template object, which is consumed by `operator=` to generate one device-side addition without creating a temporary `variable`. |
| `r += b;` | `r ← r + b`. Semantically identical to `r = r + b`, with no temporary object. |

> [!CAUTION] Caution
> `r = a + b` uses an expression template (internal type) to avoid creating a temporary `variable` that would consume additional scalar registers. Do not store the result of `a + b` in an ordinary C++ variable; otherwise, the corresponding device-side operation will not be generated.

### Conditional Operators

| Expression Syntax | Return Type | Description |
| --- | --- | --- |
| `n == imm` | `cond_expr` | Produces a condition expression object without generating any device-side operation, for use by control-flow macros such as `CCU_IF`/`CCU_WHILE`/`CCU_DO`. |
| `n != imm` | `cond_expr` | Same as above. |

> [!CAUTION] Caution
> `cond_expr` can only be consumed by control-flow macros and cannot be used as a common C++ Boolean expression (for example, `if (n == 0)`). Placing it in a common`if` statement does not generate any CCU control flow, and the expression is discarded directly.

## Usage Constraints

- It can only be constructed during the compile phase; constructing outside the compile phase throws an exception carrying `CCU_E_PTR`.
- The default constructor only allocates a virtual handle and always succeeds without throwing an exception during the compile phase; when physical scalar register resources are insufficient, the compile phase returns `CCU_E_UNAVAIL` instead of throwing an exception at construction time.
- The reserved constructor validates the reserved handle and `index`; when `index` is outside the reserved segment range (`[0, num-1]`, where `num` is the number of scalar registers reserved on the host side), it throws a `CCU_E_PARA` exception.
- The copy/move constructors only copy the `handle` field and do not allocate a new scalar register. After `variable v2 = v1;`, the two objects are the same in-kernel handle; if an independent `variable` is required, use the default constructor or [`array<variable>`](array.md).
- Do not save or compare the `handle` value outside the kernel; the handle becomes invalid after translation is complete.
- Currently only addition is supported; subtraction, multiplication, division, bitwise logic, and shift operations are not supported.
- Immediate values cannot directly participate in arithmetic; they must first be assigned to a `variable` before participating in operations: `one = 1; v = v + one`.

## Call Example

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    variable n, i, one, step;

    // Assign an immediate value (determined during the compile phase).
    n = 100;
    i = 0;
    one = 1;
    step = 8;

    // Assign between variables.
    variable cursor;
    cursor = i;           // cursor ← i

    // Arithmetic: expression template (r = a + b), generating only one device-side addition.
    variable sum;
    sum = i + step;       // sum ← i + step

    // In-place addition.
    i += one;             // i ← i + one

    // Conditional operation (used by control-flow macros such as CCU_IF/CCU_WHILE).
    CCU_IF(n == 0) {
        // ...
    }

    return CCU_SUCCESS;
}
```
