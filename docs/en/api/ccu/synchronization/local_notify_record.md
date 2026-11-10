# local_notify_record

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:42:25.530Z pushedAt=2026-11-09T06:49:22.786Z nl=2 -->

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

Sends a cross-kernel synchronization signal within the same die in a CCU kernel. It is a producer-side API. Pairing is identified by a string tag: the producer and consumer sides with the same tag are automatically bound to the same synchronization resource.

> [!NOTE] Note
> This API is implemented in C++ as the `AscendC::ccu::event_record(const char* notify_tag, uint16_t mask)` overload, which shares the same function name as `event_record(event, mask)` and is distinguished by the input parameter type. It applies to sequential synchronization between different kernels within the same die (cross-kernel on the same device), and does not apply to cross-die or cross-rank scenarios. For cross-die scenarios, use [notify_record](notify_record.md).

## Function Prototype

```cpp
// local_notify_record corresponds to the const char* overload of event_record.
CcuResult event_record(const char* notify_tag, uint16_t mask = 1);
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| notify_tag | Input | String tag. It is paired with the `notify_tag` of the consumer-side `local_notify_wait` as long as they are consistent, without the need to allocate a number in advance. It cannot be a null pointer. It must remain valid during the execution of the kernel function body (it must be a string literal or `static` storage within the kernel), and a temporary array on the stack cannot be used. |
| mask | Input | 16-bit event mask that specifies the bit to be set. The default value is `1`. Different bits of the same tag are independent of each other. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | `notify_tag` is a null pointer, or the API is called outside the compilation phase. |
| `CCU_E_NOT_SUPPORT` | Called inside a hardware loop body, which is not supported. |

## Usage Constraints

- The API cannot be called inside a hardware loop body; otherwise, `CCU_E_NOT_SUPPORT` is returned. The loop body is expanded into N copies in parallel by the hardware, and the semantics of the notify record are not unique in a parallel environment.
- `notify_tag` must remain valid during the execution of the kernel function body. Use a string literal (such as `"phase1_done"`) or a `static char[]` inside the kernel. Do not use a temporary `char[]` on the stack.
- The producer-side `local_notify_record` and the consumer-side `local_notify_wait` must use exactly the same `notify_tag` string content for pairing.
- The same tag can carry multiple pairs, distinguished by different bits of `mask`. The `mask` on the waiting side must be consistent with the one here.
- `local_notify_record` and the corresponding `local_notify_wait` must appear in pairs, and `local_notify_wait` must be located after `local_notify_record`. Any unpaired `local_notify_wait` will be blocked permanently, causing a hardware-level deadlock.

## Call Example

> In the following example, `event_record("phase1_done", 0x1)` is the `local_notify_record` described in this document. At the C++ layer, it shares the same function name as [event_record](event_record.md) (the `event` object overload), and the two are distinguished by the input parameter type (`const char*` vs `event`).

```cpp
// For simplicity, the return value handling and error handling are omitted.
using namespace AscendC::ccu;

// Scenario: On the same die, phase_producer_kernel notifies phase_consumer_kernel to continue after completing phase 1.
// Inside the CCU kernel function body on core 0
CcuResult phase_producer_kernel(ccu_kernel_arg arg) {
    // ... Phase 1 operations ...

    // Notify phase_consumer_kernel on the same die that phase 1 is complete (that is, the local_notify_record semantics).
    event_record("phase1_done", 0x1);
    return CCU_SUCCESS;
}

// Inside the CCU kernel function body on core 1
CcuResult phase_consumer_kernel(ccu_kernel_arg arg) {
    // Wait for the phase 1 completion signal sent by phase_producer_kernel.
    event_wait("phase1_done", 0x1);

    // The phase 1 result of phase_producer_kernel can now be used safely.
    return CCU_SUCCESS;
}
```
