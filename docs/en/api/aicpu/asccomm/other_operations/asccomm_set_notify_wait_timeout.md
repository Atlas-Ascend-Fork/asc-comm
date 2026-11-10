# asccomm_set_notify_wait_timeout

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:08:21.799Z pushedAt=2026-11-09T06:47:54.989Z nl=2 -->

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

Sets the default timeout duration for notification waiting on the current AI CPU execution thread. This timeout duration applies to the [asccomm_channel_notify_wait_on_thread_with_default_timeout](../synchronization_and_barrier/asccomm_channel_notify_wait_on_thread_with_default_timeout.md) and [asccomm_thread_notify_wait_on_thread_with_default_timeout](../synchronization_and_barrier/asccomm_thread_notify_wait_on_thread_with_default_timeout.md) APIs subsequently called by the current execution thread.

The [asccomm_channel_notify_wait_on_thread](../synchronization_and_barrier/asccomm_channel_notify_wait_on_thread.md) and [asccomm_thread_notify_wait_on_thread](../synchronization_and_barrier/asccomm_thread_notify_wait_on_thread.md) APIs, which specify the timeout duration manually, are not affected by this API.

## Function Prototype

```cpp
int32_t asccomm_set_notify_wait_timeout(float timeOut)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | -------------- |
| timeOut | Input | Notification wait timeout duration, in seconds.<br>**0** indicates waiting indefinitely; a value greater than 0 indicates a specified timeout duration, and a decimal value is truncated toward zero to an integer number of seconds.<br>The value cannot be negative, NaN, or greater than `UINT32_MAX`. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The default timeout duration is set successfully. |
| `ASCCOMM_E_PARA` | `timeOut` is negative, NaN, or greater than `UINT32_MAX`. |

## Usage Constraints

- The timeout duration set by this API takes effect only for the `*WithDefaultTimeout` APIs of the current AI CPU execution thread.
- The [asccomm_channel_notify_wait_on_thread](../synchronization_and_barrier/asccomm_channel_notify_wait_on_thread.md) and [asccomm_thread_notify_wait_on_thread](../synchronization_and_barrier/asccomm_thread_notify_wait_on_thread.md) APIs, which pass the timeout duration manually, are not affected by this API.
- If not set, the current implementation uses a default timeout duration of 1836 seconds.
- A non-negative floating-point `timeOut` is allowed, but it is truncated toward zero to an integer number of seconds. `ASCCOMM_E_PARA` is returned for negative values, NaN, and values greater than `UINT32_MAX`.

## Call Example

```cpp
// timeout is the wait duration selected for this example. The actual value is determined by the caller based on the synchronization policy.
float timeout = 1800.0f;  // 1800 seconds.
int32_t ret = asccomm_set_notify_wait_timeout(timeout);
```
