# ccu_buffer

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:32:47.916Z pushedAt=2026-11-09T06:49:22.777Z nl=2 -->

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

`ccu::ccu_buffer` is a C++ wrapper class for the on-chip CCU Buffer inside a CCU kernel. Each slice is fixed at 4096 bytes and is used to relay data between on-chip memory and the peer.

- Allocation upon construction: the default constructor only requests a virtual handle.
-No release upon destruction: Hardware resources are not released upon destruction. Physical resources are managed uniformly over the CCU instance lifecycle, and the virtual handle becomes invalid after the translation is complete.

> [!NOTE] Note
> Resource allocation uses a two-phase "virtual first, physical later" model: `ccu_buffer()` construction only produces a virtual handle, and the actual physical CCU Buffer slices are allocated uniformly during the compilation phase.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {
class ccu_buffer final {
public:
    ccu_buffer();                      // Default constructor. Requests the virtual handle of one CCU Buffer slice.
    ccu_buffer_handle handle{0};       // Virtual handle.
};
} // namespace ccu
} // namespace AscendC
```

## Constructor Description

- Default constructor

    Use `ccu_buffer buf;` to request the virtual handle of one CCU Buffer slice.

## Usage Constraints

- It can only be constructed during the compilation phase; constructing it outside the compilation phase throws an exception carrying `CCU_E_PTR`.
- The default constructor only requests a virtual handle and always succeeds without throwing an exception during the compilation phase; when CCU Buffer physical resources are insufficient, the compilation phase returns `CCU_E_UNAVAIL` instead of throwing an exception at construction time.
- The copy/move constructors only copy the `handle` field and do not request a new CCU Buffer slice. After `ccu_buffer b2 = b1;`, both objects point to the same slice. To obtain an independent `ccu_buffer`, you must explicitly use the default constructor or use [`array<ccu_buffer>`](array.md).
- Do not save or compare the `handle` value outside the kernel; the handle becomes invalid once translation is complete.
- Each `ccu_buffer` always represents a 4096-byte on-chip slice, and its size is not configurable.
- The `len` passed to APIs that operate on `ccu_buffer` (such as `local_copy` and `write`) must not exceed 4096 bytes. The caller must guarantee this upper limit; if it is exceeded, the hardware exhibits undefined behavior (such as data truncation or out-of-bounds access).

## Call Example

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    ccu_buffer buf;    // Request one 4 KB CCU Buffer slice.
    local_addr src;
    variable len;
    event evt;

    // Copy on-chip memory data to the CCU Buffer.
    local_copy(buf, src, len, evt);
    event_wait(evt);

    return CCU_SUCCESS;
}
```
