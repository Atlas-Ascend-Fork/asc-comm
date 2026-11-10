# address

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:35:11.973Z pushedAt=2026-11-09T06:49:22.780Z nl=2 -->

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

`ccu::address` is a C++ wrapper class for the address register in the CCU kernel. It stores address values and supports assignment and addition operations.

- Allocation upon construction: The default constructor only requests a virtual handle.
- No release upon destruction: Hardware resources are not released upon destruction. Physical resources are managed uniformly over the CCU instance lifecycle, and the virtual handle becomes invalid after translation.
- Operators for device operations: The assignment and arithmetic operators describe operations executed on the device side. During the dispatch phase, they operate on the corresponding address register rather than being computed immediately on the host side.

> [!NOTE] Note
> Resource allocation follows a two-phase "virtual first, physical later" model: the `address()` constructor only produces a virtual handle, and the actual physical address register is allocated uniformly during the compilation phase.

Unlike [`variable`](variable.md), which stores scalar values, `address` specifically stores address values, that is, physical addresses of on-chip memory. It carries device-side address values and cannot be dereferenced directly on the host side. A typical use is to add a base address and an offset to obtain a target address for data movement APIs.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {
class address final {
public:
    // Default constructor. Request one address virtual handle.
    address();

    // Assignment operator.
    void operator=(uint64_t immediate) const;                   // Assign an immediate address.
    void operator=(const variable& var) const;                  // Assign the variable value to address.
    void operator=(const address& other) const;                 // Assign between address objects.

    // Arithmetic operators.
    void operator+=(const variable& var) const;                 // Add variable to address in place.
    void operator+=(const address& other) const;                // Add address to address in place.
    /*Internal expression object*/ operator+(const address& that) const;    // address + address
    /*Internal expression object*/ operator+(const variable& var) const;    // address + variable

    ccu_address_handle handle{0};                                 // Virtual handle.
};
// variable + address (commutative, global function)
/*Internal expression object*/ operator+(const variable& var, const address& addr);
} // namespace ccu
} // namespace AscendC
```

## Constructor Description

- Default constructor

    Use `address a;` to request one `address` virtual handle.

## Operator Description

### Assignment Operators

| Expression Syntax | Hardware Semantics |
| --- | --- |
| `addr = imm;` (`imm` is `uint64_t`) | `addr ← imm`. The address immediate is determined during the compilation phase and is immutable during the dispatch phase. |
| `addr = var;` (`var` is `variable`) | `addr ← var`. Writes the value of the variable during the dispatch phase to the address register, suitable for dynamic addresses during the dispatch phase. |
| `dst = src;` (`src` is `address`) | `dst ← src`. Register assignment between address objects. |

### Arithmetic Operators

| Expression Syntax | Hardware Semantics |
| --- | --- |
| `r = a + b;` (`a`/`b` are both `address`) | `r ← a + b`. |
| `r = addr + var;` / `r = var + addr;` | `r ← addr + var`. The two syntaxes have the same semantics (commutative law). |
| `addr += var;` (`var` is `variable`) | `addr ← addr + var` (in-place operation, saving one instruction compared with `addr = addr + var`). |
| `addr += other;` (`other` is `address`) | `addr ← addr + other`. |

## Usage Constraints

- It can only be constructed during the compilation phase; constructing outside the compilation phase throws an exception carrying `CCU_E_PTR`.
- The default constructor only requests a virtual handle and always succeeds without throwing an exception during the compilation phase; when `address` physical resources are insufficient, the compilation phase returns `CCU_E_UNAVAIL` instead of throwing at construction time.
- The copy/move constructors only copy the `handle` field and do not request a new `address`; after `address a2 = a1;`, both objects point to the same address register. `operator=(const address&)` issues a device-side register assignment instruction. To obtain an independent `address`, you must explicitly use the default constructor.
- Do not save or compare the `handle` value outside the kernel; the handle becomes invalid after translation completes.
- `address` cannot be assigned to `variable`, that is, the `variable = address` operation is not provided.
- Currently, only addition is supported. Subtraction, multiplication, and division are not supported.
- An immediate cannot directly participate in arithmetic (`addr + 0x100` is invalid). You must first assign the offset to a `variable` before using it in an operation.

## Call Example

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    address base, dst;
    variable offset, stride;

    // Assign an immediate address (fixed at compile time).
    base = 0x80000000ULL;

    // Assign a variable value to address (dynamic address at dispatch time).
    // offset is injected through load_arg at dispatch time.
    load_arg(offset, 0);
    dst = offset;         // dst ← offset (determined at dispatch time).

    // address + variable offset (two equivalent forms).
    stride = 4096;
    dst = base + stride;  // r = addr + var
    dst = stride + base;  // r = var + addr (equivalent).

    // In-place offset of address.
    base += stride;        // base += 4096

    // address + address
    address result;
    result = base + dst;

    return CCU_SUCCESS;
}
```
