# event

<!-- md-trans-meta sourceCommit=5f8b41dd145ea0a95acdd2f2f2ea4995d681fdd0 translatedAt=2026-11-05T01:32:46.578Z pushedAt=2026-11-09T06:49:22.778Z nl=2 -->

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

`ccu::event` is a C++ wrapper class for synchronization register slots in the CCU kernel, used to mark the completion status of asynchronous operations (such as data transfers).

- Allocation upon construction: the default constructor only requests a virtual handle.
-No release upon destruction: Hardware resources are not released upon destruction. Physical resources are managed uniformly over the CCU instance lifecycle, and the virtual handle becomes invalid after the translation completes.

> [!NOTE] Note
> Resource allocation uses a two-phase "virtual first, physical later" model: the `event()` constructor only produces a virtual handle, and the actual physical event slot is allocated uniformly during the compile phase.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {
class event final {
public:
    // Default constructor: request one virtual handle for an event slot.
    event();

    // Reserved constructor: bind to the index-th event slot reserved on the host side, without requesting a new event slot.
    explicit event(ccu_event_handle acq_handle, uint32_t index = 0);

    ccu_event_handle handle{0};  // Virtual handle.
};
} // namespace ccu
} // namespace AscendC
```

## Constructor Description

- Default constructor

    Use `event e;` to request a virtual handle for one completion event slot.

- Reserved constructor

    The parameters of the reserved constructor are as follows:

    | Parameter | Input/Output | Description |
    | --- | --- | --- |
    | acq_handle | Input | Reservation handle, that is, the handle returned after the host side calls `asccomm_ccu_event_alloc` to reserve a segment of physical event slots from the instance resource pool in advance, and passes it to the kernel through `kernelArg`. |
    | index | Input | Index of the event slot to bind within the reserved segment. The default value is `0`, and the value range is `[0, num-1]` (`num` is the number of event slots reserved by `asccomm_ccu_event_alloc` on the host side). If the value is out of this range, a `CCU_E_PARA` exception is thrown. |

    Constructing multiple `event` objects with the same reservation handle and the same `index` makes them share the same physical event slot.

## Usage Constraints

- Construction is allowed only in the compile phase. Constructing outside the compile phase throws an exception carrying `CCU_E_PTR`.
- The default constructor only requests a virtual handle and always succeeds without throwing an exception in the compile phase. When physical event slot resources are insufficient, the compile phase returns `CCU_E_UNAVAIL` instead of throwing an exception at construction time.
- The reserved constructor validates the reservation handle and `index`. If `index` is out of the reserved segment range (`[0, num-1]`, where `num` is the number of event slots reserved on the host side), a `CCU_E_PARA` exception is thrown.
- The copy/move constructors only copy the `handle` field and do not request a new event slot. After `event e2 = e1;`, the two objects are handles within the same kernel, and `event_record`/`event_wait` on either object operates on the same hardware status bit. To obtain an independent `event`, use the default constructor or [`array<event>`](array.md).
- Do not save or compare the `handle` value outside the kernel. The handle becomes invalid after translation is complete.
- `event` has no `mask` field. `mask` is passed as a parameter in `event_record`/`event_wait` and data movement APIs, with a C++ default value of `1`. Different bits of the same `event` are independent of each other and can carry multiple pairs.

## Call Example

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    event evt;    // Request one event slot.

    // When used with data transfer APIs: the flag bit corresponding to mask in evt is automatically set to 1 when hardware transfer completes.
    local_addr src, dst;
    variable len;
    local_copy(dst, src, len, evt);    // mask defaults to 0x1.
    event_wait(evt);                    // Wait until bit 0 is set to 1.

    return CCU_SUCCESS;
}
```
