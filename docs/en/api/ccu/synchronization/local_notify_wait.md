# local_notify_wait

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:43:02.815Z pushedAt=2026-11-09T06:49:22.787Z nl=2 -->

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

Blocks and waits for a cross-kernel synchronization signal in the same die within the CCU kernel, with kernels paired by a string tag. It is a consumer-side API. When hardware execution reaches this point, it blocks until the corresponding bit is set.

> [!NOTE] Note
> This API is implemented at the C++ layer as an overload of `AscendC::ccu::event_wait(const char* notify_tag, uint16_t mask)`. It shares the same function name as `event_wait(event, mask)` and is distinguished by the input parameter type. It applies to sequential synchronization between different kernels within the same die, but not to cross-die or cross-rank scenarios. For cross-die scenarios, use [notify_wait](notify_wait.md).

## Function Prototype

```cpp
// local_notify_wait corresponds to the const char* overload of event_wait.
CcuResult event_wait(const char* notify_tag, uint16_t mask = 1);
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| notify_tag | Input | String tag that must exactly match the `notify_tag` of the producer-side `local_notify_record` to complete pairing. It cannot be a null pointer. The constraints are the same as those on the producer side: it must be a string literal or `static` storage within the kernel. |
| mask | Input | 16-bit event mask that specifies the bit to wait for. The default value is `1`. It must match the `mask` of the producer-side `local_notify_record`. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | `notify_tag` is a null pointer, or the API is called outside the compilation phase. |

## Usage Constraints

- `local_notify_wait` can be called inside a hardware loop body.
- `notify_tag` must remain valid during the execution of the kernel function body. The constraints are the same as those of `local_notify_record`.
- Before the call, a corresponding `local_notify_record` must already exist at a preceding position; otherwise, the call is blocked permanently and causes a hardware-level deadlock.
- The `mask` to wait for must be consistent with the `mask` passed to `local_notify_record` on the producer side.

## Call Example

> In the following example, `event_wait("phase1_done", 0x1)` is the `local_notify_wait` described in this document. At the C++ layer, it shares the same function name as [event_wait](event_wait.md) (the `event` object overload), and the two are distinguished by the input parameter type (`const char*` vs `event`).

```cpp
// For simplicity, the return value reception and error handling are omitted.
using namespace AscendC::ccu;

// Scenario: Within the same die, phase_consumer_kernel waits for phase_producer_kernel to complete phase 1 before starting phase 2.
// Inside the CCU kernel function body on core 1.
CcuResult phase_consumer_kernel(ccu_kernel_arg arg) {
    // Wait for bit0 of the "phase1_done" signal sent by phase_producer_kernel (that is, the local_notify_wait semantics).
    event_wait("phase1_done", 0x1);

    // After this, the result of phase 1 of phase_producer_kernel can be safely used.
    // ... Phase 2 operations ...
    return CCU_SUCCESS;
}
```
