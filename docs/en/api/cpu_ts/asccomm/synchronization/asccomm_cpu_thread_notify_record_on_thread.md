# asccomm_cpu_thread_notify_record_on_thread

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

Submits a Notify record task for the Notify of a target AI CPU thread. It is used by the Host CPU to send a startup or doorbell notification to the AI CPU. This API submits the record task to the Host Stream associated with `thread`; the runtime executes the record operation on that stream.

## Function Prototype

```cpp
int32_t asccomm_cpu_thread_notify_record_on_thread(uint64_t thread, uint64_t dstThread, uint32_t dstNotifyIdx)
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Host CPU thread object address. The control plane creates and exports the CPU_TS thread resource before passing it in; it cannot be 0. The type is `uint64_t`. |
| dstThread | Input | Target AI CPU thread object address. The control plane creates the AI CPU thread resource and exports it to the CPU_TS engine before passing it in; it cannot be 0. The type is `uint64_t`. |
| dstNotifyIdx | Input | Notify index of the target AI CPU thread. The value range is `[0, number of Notify resources in dstThread)`. |

## Return Value

The return value is `int32_t`. The values are described as follows.

| Return Value | Description |
| --- | --- |
| `0` | The Notify record task was successfully submitted to the Host Stream; this does not indicate that the target Notify has already received the synchronization signal. |
| `1` | `thread`, `dstThread`, or a Notify resource parameter is invalid, or thread object layout validation failed. |
| `2` | The runtime environment does not provide the underlying Notify record interface; this API returns this value before submitting the task. |
| Other values | Original error code returned by the underlying Notify record interface when task submission fails. This API passes the value through unchanged. |

## Constraints

- `thread` must come from a valid CPU_TS thread resource created by the control plane, and `dstThread` must come from a valid AI CPU thread resource exported to the CPU_TS engine.
- `dstNotifyIdx` must be smaller than the number of Notify resources in the target AI CPU thread, and the corresponding Notify resource must be valid.
- The caller must keep the thread and Notify resources alive until the task is complete.
- This API only submits a Notify record task. It does not replace data movement or stream synchronization between the Host CPU and AI CPU.
- A thread object does not support concurrent calls to this API from multiple threads; the execution order of tasks is guaranteed by the Host Stream associated with `thread`.

## Example

```cpp
// thread is a Host CPU thread object address exported by the control plane;
// dstThread is an AI CPU thread object address exported to the CPU_TS engine;
// dstNotifyIdx is the Notify index waited on by the AI CPU kernel.
int32_t ret = asccomm_cpu_thread_notify_record_on_thread(thread, dstThread, dstNotifyIdx);
if (ret != 0) {
    // Handle the submission failure with the upper-layer error handling flow.
}
```

For complete usage scenarios, see the custom AI CPU operator samples in the [aicpu_ts_direct](../../../../../../examples/aicpu_ts/aicpu_ts_direct/README.md) directory.
