# asccomm_channel_notify_wait_on_thread_with_default_timeout

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:20:54.252Z pushedAt=2026-11-09T06:47:54.998Z nl=2 -->

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

Creates a local Notify wait task that waits for the synchronization signal on the specified channel, using the default timeout set by [asccomm_set_notify_wait_timeout](../other_operations/asccomm_set_notify_wait_timeout.md).

## Function Prototype

```cpp
int32_t asccomm_channel_notify_wait_on_thread_with_default_timeout(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| channel | Input | Communication channel handle. After the control plane creates the channel resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ChannelHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| localNotifyIdx | Input | Local Notify index. The current implementation requires `localNotifyIdx < localNotifyNum - 1` and uses the Notify corresponding to `localNotifyIdx + 1` in the local Notify array. Item 0 is reserved for internal use. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The notify wait task has been created. It does not mean the synchronization signal has arrived. |
| `ASCCOMM_E_PTR` | The thread or channel handle is 0, or the thread resource or local task cache pointer is null. |
| `ASCCOMM_E_PARA` | The thread/channel resource, local Notify index, or Notify resource parameter is invalid. |
| `ASCCOMM_E_TIMEOUT` | When submitting the task, waiting for the communication task submission queue space timed out. It does not mean the Notify wait task timed out. |
| `ASCCOMM_E_INTERNAL` | Internal execution failed, such as task creation, driver query or configuration, or local task cache operations. |

## Usage Constraints

- `localNotifyIdx` must satisfy `localNotifyIdx < localNotifyNum - 1`, where `localNotifyNum` is the number of local Notify entries in the channel resource. Item 0 is reserved for internal use, and the Notify corresponding to `localNotifyIdx + 1` is actually used.
- The default timeout is set in the current AI CPU execution thread by [asccomm_set_notify_wait_timeout](../other_operations/asccomm_set_notify_wait_timeout.md). If not set, the current implementation uses 1836 seconds.
- A default timeout of 0 means waiting forever, and a value greater than 0 means the specific number of seconds to wait.
- The submission timing of the notify wait task is consistent with the submission policy of the thread resource. In normal mode, it can be submitted immediately; in batch mode or when the cache is full, it is submitted according to the current implementation.
- The default timeout applies only to the Notify wait task itself. This API returns immediately after submitting the wait task, and the Notify wait timeout is not reported through the return value.

## Call Example

```cpp
// thread is the valid ThreadHandle resource handle passed by the control plane; channel is the valid ChannelHandle resource handle passed by the control plane; localNotifyIdx is the local Notify index.
// The default timeout has been set by asccomm_set_notify_wait_timeout.
uint32_t localNotifyIdx = 0;
int32_t ret = asccomm_channel_notify_wait_on_thread_with_default_timeout(thread, channel, localNotifyIdx);
```
