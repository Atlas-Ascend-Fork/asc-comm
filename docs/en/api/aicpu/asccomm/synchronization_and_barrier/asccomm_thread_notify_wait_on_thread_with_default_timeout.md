# asccomm_thread_notify_wait_on_thread_with_default_timeout

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:23:40.830Z pushedAt=2026-11-09T06:47:55.001Z nl=2 -->

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

Generates a local Notify wait task that waits for the synchronization signal on the specified thread, using the default timeout set by [asccomm_set_notify_wait_timeout](../other_operations/asccomm_set_notify_wait_timeout.md).

This API must be used together with [asccomm_thread_notify_record_on_thread](./asccomm_thread_notify_record_on_thread.md). To manually specify a timeout, call [asccomm_thread_notify_wait_on_thread](./asccomm_thread_notify_wait_on_thread.md).

## Function Prototype

```cpp
int32_t asccomm_thread_notify_wait_on_thread_with_default_timeout(ThreadHandle thread, uint32_t notifyIdx)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| notifyIdx | Input | Local Notify index. The value range is `[0, the number of Notify resources in the Thread resource)`. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The Notify wait task has been generated; this does not mean the synchronization signal has arrived. |
| `ASCCOMM_E_PTR` | The thread handle is 0, or the thread resource or local task cache pointer is null. |
| `ASCCOMM_E_PARA` | The thread resource, communication task submission queue, Notify index, or Notify resource parameter is invalid. |
| `ASCCOMM_E_TIMEOUT` | When triggering task submission, waiting for communication task submission queue space timed out; this does not mean the Notify wait task timed out. |
| `ASCCOMM_E_INTERNAL` | Internal execution failure in task generation, driver query or configuration, or local task cache operations. |

## Usage Constraints

- `notifyIdx` must be less than the number of Notify resources in the local thread resource.
- The default timeout is set in the current AI CPU execution thread through [asccomm_set_notify_wait_timeout](../other_operations/asccomm_set_notify_wait_timeout.md); if not set, the current implementation uses 1836 seconds.
- A default timeout of 0 means waiting forever, and a value greater than 0 means the specific number of seconds to wait.
- The submission timing of the Notify wait task is consistent with the submission policy of the thread resource; in normal mode it can be submitted immediately, and in batch mode or when the cache is full it is submitted according to the current implementation.
- The default timeout applies only to the Notify wait task itself. This API returns immediately after submitting the wait task, and a Notify wait timeout is not reported through the return value.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in by the control plane; notifyIdx is the local Notify index.
// The default timeout has been set by asccomm_set_notify_wait_timeout.
uint32_t notifyIdx = 0;
int32_t ret = asccomm_thread_notify_wait_on_thread_with_default_timeout(thread, notifyIdx);
```
