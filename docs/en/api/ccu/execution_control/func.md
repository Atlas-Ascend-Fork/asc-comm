# func

<!-- md-trans-meta sourceCommit=7c8e828620339c357fd0fa3ee87abd5b7000c562 translatedAt=2026-11-04T10:43:23.193Z pushedAt=2026-11-09T06:47:55.016Z nl=2 -->

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

`ccu::func` wraps a lambda into a reusable function block (FuncBlock): at construction, it only records the lambda without generating instructions; when first called through [call_func](call_func.md), it executes the lambda and synthesizes the logic into a single instruction block; each subsequent call only appends a jump instruction and reuses the same instruction block, thereby saving instruction space. The lambda has zero or more `ccu::variable` parameters, which are bound to arguments at calls, and returns `void`.

> [!NOTE] Note
> This API only defines the function block. For how to call it, see [call_func](call_func.md).

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {

class func {
public:
    // The lambda must return void, and all its parameters must be ccu::variable.
    template <typename Lambda>
    explicit func(Lambda body);

    func(const func &) = delete;             // Non-copyable.
    func &operator=(const func &) = delete;  // Non-assignable.
};

} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| body | Input | Lambda expression whose parameters are zero or more `ccu::variable` objects and whose return type is `void`. The number of parameters is deduced automatically from the lambda signature at compilation time. |

## Return Value

None.

## Usage Constraints

- `ccu::func` is non-copyable: the copy constructor and assignment operator are `= delete`, so each `ccu::func` object has a unique identity.
- The lambda parameter types must be passable from `ccu::variable&`, and can be written as `ccu::variable`, `ccu::variable&`, or `const ccu::variable&`. Other types such as `int` and `ccu::address` cause a compilation error.
- lambda must return `void`; otherwise, a compilation error occurs.
- Different `ccu::func` objects synthesize independent FuncBlocks even if their lambda content is identical.

## Call Example

```cpp
using namespace AscendC::ccu;

static func add_dbl([](variable x) {
    variable tmp;
    tmp = x + x;   // Double x.
});
```
