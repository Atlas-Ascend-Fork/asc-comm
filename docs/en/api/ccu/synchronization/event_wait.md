# event_wait

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:40:55.749Z pushedAt=2026-11-09T06:49:22.785Z nl=2 -->

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

Blocks and waits for the `event` flag bits corresponding to `mask` in the local synchronization register until all of them are set to **1**, then continues execution and automatically clears these flag bits. These flag bits are set by [event_record](event_record.md) or the [data movement API](../data_movement/README.md) in the same kernel.

> [!NOTE] Note
> After each successful wait by `event_wait`, the corresponding flag bits are automatically cleared. To wait for the same flag bits again, they must be set again by the producer side first. Different flag bits of the same `event` are independent of each other; setting and waiting for each does not affect the others.

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {
CcuResult event_wait(event e, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| e | Input | Local event object, corresponding to an event slot in the synchronization register. A virtual handle is automatically requested during default construction, and physical resources are uniformly allocated during the compilation phase. |
| mask | Input | 16-bit event mask that specifies the `event` flag bits to wait for. The default value is `1`, that is, bit0. Waiting for multiple bits is supported, for example, `mask = 0x3` means waiting for both bit0 and bit1 to be set. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation phase. |
| `CCU_E_NOT_FOUND` | The passed `e` handle is not created in the current kernel. |

## Usage Constraints

- `event_wait` can be called inside the loop body of a hardware loop.
- Before calling this API, the corresponding flag bits must have been set. They can be set automatically by the [data movement API](../data_movement/README.md) when the data movement is complete, or explicitly by calling [event_record](event_record.md); otherwise, this API blocks permanently, causing a hardware-level deadlock.
- The `mask` of this API must be consistent with the `mask` passed by the producer side; otherwise, the signal will never be received.

## Call Example

```cpp
// For simplicity, return value reception and error handling are omitted.
using namespace AscendC::ccu;

// Scenario: Wait for the copy from the local on-chip memory to the on-chip memory to complete before continuing.
// Inside the CCU kernel function body.
CcuResult my_kernel(ccu_kernel_arg arg) {
    local_addr src, dst;
    variable len;
    event evt;

    // Initiate an asynchronous copy. When the hardware completes, it automatically sets the flag bit corresponding to mask in evt to 1.
    local_copy(dst, src, len, evt, 0x1);

    // Block and wait for the copy to complete.
    event_wait(evt, 0x1);

    // After this, the data pointed to by dst can be safely used.
    return CCU_SUCCESS;
}
```
