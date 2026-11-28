# asccomm_cpu_thread_notify_wait_on_thread

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR&950DT series products: supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 series products: not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 series products: not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products: not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: not supported
<!-- end id6 -->

## Description

Header file path: `"cpu_ts/asccomm_cpu_primitives.h"`.

Submits the wait task for a specified Notify on the local Host CPU thread to the Host Stream associated with `thread` and returns immediately. It is typically used to wait for the completion notification returned by an AI CPU executor after communication or computation.

## Function Prototype

```cpp
int32_t asccomm_cpu_thread_notify_wait_on_thread(uint64_t thread, uint32_t notifyIdx, uint32_t timeOut)
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Host CPU thread object address. The control plane creates and exports the CPU_TS thread resource before passing it in; it cannot be 0. The type is `uint64_t`. |
| notifyIdx | Input | Local Notify index. The value range is `[0, number of Notify resources in thread)`. |
| timeOut | Input | Timeout duration of the Notify wait task, in seconds. It must be greater than 0; when it is 0, the wait task times out immediately during execution instead of waiting indefinitely. |

## Return Value

The return value is `int32_t`. The values are described as follows.

| Return Value | Description |
| --- | --- |
| `0` | The Notify wait task was successfully submitted to the Host Stream; this does not indicate that the specified Notify has received the synchronization signal or that the wait task has finished. |
| `1` | `thread`, `notifyIdx`, or a Notify resource parameter is invalid, or thread object layout validation failed. |
| `2` | The runtime environment does not provide the underlying Notify wait interface; this API returns this value before submitting the wait task. |
| Other values | Original error code returned by the underlying Notify wait interface when wait task submission fails. This API passes the value through unchanged. |

## Constraints

- `thread` must come from a valid CPU_TS thread resource created by the control plane.
- `notifyIdx` must be smaller than the number of Notify resources in the local thread, and the corresponding Notify resource must be valid.
- `timeOut` must be greater than 0; when it is 0, the wait task times out immediately during execution instead of waiting indefinitely.
- `timeOut` applies only to the Notify wait task executed on the Host Stream. This API does not synchronously wait for the task to finish.
- This API must be used with [asccomm_cpu_thread_notify_record_on_thread](asccomm_cpu_thread_notify_record_on_thread.md). On the AI CPU side, use [asccomm_thread_notify_record_on_thread](../../../aicpu_ts/asccomm/synchronization_and_barrier/asccomm_thread_notify_record_on_thread.md) of AICPU_TS Asccomm to return the notification to the Host CPU thread.
- The caller must keep the thread and Notify resources alive until the wait task is complete.
- A thread object does not support concurrent calls to this API from multiple threads; the execution order of tasks is guaranteed by the Host Stream associated with `thread`.

## Example

```cpp
// thread is a Host CPU thread object address exported by the control plane;
// notifyIdx is the local Notify index used for the AI CPU completion notification;
// timeout is in seconds and must be greater than 0.
constexpr uint32_t timeout = 1836;
int32_t ret = asccomm_cpu_thread_notify_wait_on_thread(thread, notifyIdx, timeout);
if (ret != 0) {
    // Handle the submission failure with the upper-layer error handling flow.
}
```

For complete usage scenarios, see the custom AI CPU operator samples in the [aicpu_ts_direct](../../../../../../examples/aicpu_ts/aicpu_ts_direct/README.md) directory.
