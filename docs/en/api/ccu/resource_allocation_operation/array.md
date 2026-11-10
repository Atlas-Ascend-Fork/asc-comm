# array

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:32:51.843Z pushedAt=2026-11-09T06:49:22.779Z nl=2 -->

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

`ccu::array<T>` is a C++ template class for batch holding of physically contiguous resources within a CCU kernel.

- Batch allocation on construction: `array(count)` requests `count` physically contiguous virtual handles at once.
- `array(acq_handle, count)` binds the first `count` resources reserved on the host side and does not request new physical resources.
- No release on destruction: Hardware resources are not released upon destruction. The virtual handle becomes invalid after translation completes, and the physical resource is managed and reclaimed uniformly over the lifecycle of the CCU instance.
- Move-only: copying is prohibited, while moving is allowed.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {
template <typename T>  // T supports only variable, event, and ccu_buffer.
class array final {
public:
    explicit array(uint32_t count);       // Batch virtual allocation on construction. count can be 0.
    // Bind the first count resources reserved on the host side. Available only for variable/event specializations.
    array(typename ccu_array_traits<T>::handle_t acq_handle, uint32_t count);
    T& operator[](uint32_t i);            // Subscript access (no bounds checking).
    const T& operator[](uint32_t i) const;
    T* data();                             // Obtain the pointer to the first element (for passing to batch APIs that require a pointer parameter).
    const T* data() const;
    uint32_t size() const;                 // Returns the number of elements.
    // Copying is prohibited; moving is allowed.
    array(const array&) = delete;
    array& operator=(const array&) = delete;
    array(array&& other) noexcept;
    array& operator=(array&& other) noexcept;
};
} // namespace ccu
} // namespace AscendC
```

## Parameters

Currently, only the following three specialized types are supported:

| Specialized Type | Resource Description |
| --- | --- |
| `array<variable>` | N physically contiguous scalar registers |
| `array<event>` | N physically contiguous event units |
| `array<ccu_buffer>` | N physically contiguous CCU Buffers |

## Constructor Description

- Batch construction

    | Construction Form | Description |
    | --- | --- |
    | `array<variable> vars(count);` | Requests `count` physically contiguous scalar register handles. |
    | `array<event> evts(count);` | Requests `count` physically contiguous event unit handles. |
    | `array<ccu_buffer> bufs(count);` | Requests `count` physically contiguous CCU Buffer handles. |

- Reservation-based construction

    | Construction Form | Description |
    | --- | --- |
    | `array<variable> vars(acq_handle, count);` | Binds the first `count` scalar registers reserved on the host side and does not request new scalar registers. |
    | `array<event> evts(acq_handle, count);` | Binds the first `count` event units reserved on the host side and does not request new event units. |

    The type of the `acq_handle` parameter in the constructor must correspond to `T`:
    - `array<variable>` uses the reservation handle returned by `asccomm_ccu_variable_alloc`;
    - `array<event>` uses the reservation handle returned by `asccomm_ccu_event_alloc`.

    During construction, the resources with sequence numbers `0` to `count - 1` in the reserved segment are bound in order. Element `arr[i]` corresponds to the `i`-th resource in the reserved segment, in one-to-one correspondence with the `index` in `asccomm_ccu_variable_get_addr` and `asccomm_ccu_event_get_addr`.

## Member Function Description

| Function | Description |
| --- | --- |
| `arr[i]` | Returns the reference to the `i`-th element (starting from 0, without bounds checking). |
| `arr.data()` | Returns a pointer to the first element, used for passing to batch APIs that require a `T*` parameter. |
| `arr.size()` | Returns the `count` value at the time of allocation. |

## Usage Constraints

- `array(count)` and `array(acq_handle, count)` can only be constructed at compile time.
- `array(count)` always succeeds. When the resource pool cannot provide `count` contiguous physical resources, `CCU_E_UNAVAIL` is returned at compile time instead of being thrown at construction time.
- `array<T>` is specialized only for the three types `variable`, `event`, and `ccu_buffer`. Reserved construction is available only for `variable` and `event`. Instantiating other types or using reserved construction for them fails at compile time.
- When `count > 0`, `array(acq_handle, count)` validates the reserved handle and `count`. If the parameters are invalid, an exception is thrown, caught uniformly by `<<<>>>`, and `CCU_E_INTERNAL` is returned externally, aborting the current compilation round.
- `count` must not exceed the number of resources under the reserved handle. When `count` is 0, an empty `array` is constructed directly (`size() == 0`), with no resources bound and no validation of `acq_handle`.
- Do not save the `handle` value of an element outside the kernel. The handle becomes invalid after translation is complete.

## Call Example

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    // Request four physically contiguous variables in a batch.
    array<variable> v_arr(4);

    // Request four physically contiguous ccu_buffers in a batch.
    array<ccu_buffer> bufs(4);

    // Access a single element by subscript.
    v_arr[0] = 1024;    // Assign an immediate value to the 0th variable.

    return CCU_SUCCESS;
}
```

Usage of binding resources reserved on the host side:

```cpp
using namespace AscendC::ccu;

// After the host reserves resources through asccomm_ccu_variable_alloc/asccomm_ccu_event_alloc,
// put the reserved handles and counts into kernelArg and pass them to the kernel.
struct my_kernel_arg {
    ccu_variable_handle acq_handle;
    uint32_t var_num;
    ccu_event_handle acq_event_handle;
    uint32_t event_num;
};

CcuResult my_kernel(ccu_kernel_arg arg) {
    auto* args = static_cast<my_kernel_arg*>(arg);

    // Bind the var_num scalar registers reserved on the host without requesting new scalar registers. The bound variables are used in the same way as
    // ordinary variables. Here, subscript access is demonstrated by assigning values one by one.
    array<variable> vars(args->acq_handle, args->var_num);
    for (uint32_t i = 0; i < vars.size(); i++) {
        vars[i] = i;
    }

    // Bind the event_num event units reserved on the host. In this example, event_record is performed on the CCU side.
    array<event> events(args->acq_event_handle, args->event_num);
    for (uint32_t i = 0; i < events.size(); i++) {
        event_record(events[i]);
    }

    return CCU_SUCCESS;
}
```
