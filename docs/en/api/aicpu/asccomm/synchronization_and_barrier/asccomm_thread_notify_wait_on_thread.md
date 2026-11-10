# asccomm_thread_notify_wait_on_thread

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:25:04.617Z pushedAt=2026-11-09T06:47:55.003Z nl=2 -->

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

Creates a local Notify waiting task to wait for the synchronization signal on the specified thread.

## Function Prototype

```cpp
int32_t asccomm_thread_notify_wait_on_thread(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| notifyIdx | Input | Local Notify index. The value range is `[0, the number of Notify resources in the thread resource)`. |
| timeOut | Input | Waiting timeout, in seconds. **0** means waiting indefinitely, and a value greater than 0 means the specified timeout. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The Notify waiting task has been created; this does not mean the synchronization signal has arrived. |
| `ASCCOMM_E_PTR` | The thread handle is 0, or the thread resource or local task cache pointer is null. |
| `ASCCOMM_E_PARA` | The thread resource, communication task submission queue, Notify index, or Notify resource parameter is invalid. |
| `ASCCOMM_E_TIMEOUT` | When triggering task submission, waiting for space in the communication task submission queue timed out; this does not mean the Notify waiting task timed out. |
| `ASCCOMM_E_INTERNAL` | Internal execution failed, such as task creation, driver query or configuration, or local task cache operations. |

## Usage Constraints

- `notifyIdx` must be less than the number of Notify resources in the local thread resource.
- This API must be used together with [asccomm_thread_notify_record_on_thread](asccomm_thread_notify_record_on_thread.md).
- The submission timing of the Notify waiting task is consistent with the submission policy of the thread resource. In normal mode, it can be submitted immediately; in batch mode or when the cache is full, it is submitted according to the current implementation.
- `timeOut` applies only to the Notify waiting task itself. This API returns immediately after submitting the waiting task, and a Notify waiting timeout is not reported through the return value.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in by the control plane; notifyIdx is the local Notify index.
uint32_t notifyIdx = 0;
uint32_t timeout = 1800;  // Unit: seconds
int32_t ret = asccomm_thread_notify_wait_on_thread(thread, notifyIdx, timeout);
```
