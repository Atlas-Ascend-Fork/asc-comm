# event_record

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:40:11.986Z pushedAt=2026-11-09T06:49:22.784Z nl=2 -->

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

Explicitly sets the event flag bit corresponding to `mask` in the local synchronization register to **1**, indicating that the current operation or phase is complete, so that [event_wait](event_wait.md) within the same kernel can read the completion signal.

> [!NOTE] Note
> In most scenarios, you do not need to explicitly call this API. You need to call it explicitly only when no copy API is called at a certain position, for example, when the copy is skipped because the data volume is 0, or when a branch code segment contains only computation. In these cases, the corresponding bit in the local `event` is not automatically set, so you must call this API to set it; otherwise, the downstream `event_wait` blocks forever.

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {
CcuResult event_record(event e, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| e | Input | Local event object, corresponding to an event slot in the synchronization register. A virtual handle is automatically requested during default construction, and physical resources are allocated uniformly during the compilation phase. |
| mask | Input | 16-bit event mask that specifies the `event` flag bit to set. The default value is `1`, that is, bit0. Different flag bits of the same `event` object are independent of each other and can be used separately for pairing multiple groups of `event_record` and `event_wait`. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation phase. |
| `CCU_E_NOT_FOUND` | The passed `e` handle is not created in the current kernel. |
| `CCU_E_NOT_SUPPORT` | Calling this API inside the loop body of a hardware loop is not supported. |

## Usage Constraints

- This API cannot be called inside the loop body of a hardware loop; otherwise, `CCU_E_NOT_SUPPORT` is returned. The loop body is replicated into multiple copies for parallel execution, and each copy executes `event_record` once, making the set semantics ambiguous.
- After the bit is set, an [event_wait](event_wait.md) must consume the signal; otherwise, this set operation is not used.
- The `mask` of `event_record` and `event_wait` must be consistent; otherwise, `event_wait` never receives the signal.

## Call Example

```cpp
// For simplicity, return value reception and error handling are omitted.
using namespace AscendC::ccu;

// Inside the CCU kernel function body.
CcuResult my_kernel(ccu_kernel_arg arg) {
    event evt;

    // After performing some operations, manually mark bit0 as complete.
    event_record(evt, 0x1);

    // The downstream waits for bit0 to be set.
    event_wait(evt, 0x1);
    return CCU_SUCCESS;
}
```
