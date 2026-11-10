# asccomm_thread_notify_record_on_thread

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:22:09.782Z pushedAt=2026-11-09T06:47:55.000Z nl=2 -->

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

Header file path: `"aicpu/hcomm/asccomm_primitives.h"`.

Sends a synchronization signal to the Notify of the target thread. It is mainly used in multi-thread synchronization wait scenarios.

## Function Prototype

```cpp
int32_t asccomm_thread_notify_record_on_thread(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| dstThread | Input | Target thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| dstNotifyIdx | Input | Notify index of the target thread. The value range is `[0, the number of Notify resources in the dstThread resource)`. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The notification task has been generated; it does not mean that the target Notify has received the synchronization signal. |
| `ASCCOMM_E_PTR` | `thread` or `dstThread` is 0, or the thread resource or local task cache pointer is null. |
| `ASCCOMM_E_PARA` | The thread resource, communication task submission queue, target Notify index, or Notify resource parameter is invalid. |
| `ASCCOMM_E_TIMEOUT` | Timed out waiting for space in the communication task submission queue when triggering task submission. |
| `ASCCOMM_E_INTERNAL` | Internal execution failed, such as task generation, driver query or configuration, or local task cache operation. |

## Usage Constraints

- `dstNotifyIdx` must be less than the number of Notify resources of the target thread resource, and the corresponding Notify resource must be of a type supported by the current implementation.
- This API must be used together with [asccomm_thread_notify_wait_on_thread](asccomm_thread_notify_wait_on_thread.md).
- The timing of notification task submission is consistent with the submission policy of the thread resource; in normal mode it can be submitted immediately, while in batch mode or when the cache is full it is submitted according to the current implementation.

## Call Example

```cpp
// thread and dstThread are both valid ThreadHandle resource handles passed in by the control plane.
uint32_t notifyIdx = 0;
int32_t ret = asccomm_thread_notify_record_on_thread(thread, dstThread, notifyIdx);
```
