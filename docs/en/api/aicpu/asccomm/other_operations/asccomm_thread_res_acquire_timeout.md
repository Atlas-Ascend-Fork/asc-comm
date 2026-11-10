# asccomm_thread_res_acquire_timeout

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:08:12.551Z pushedAt=2026-11-09T06:47:54.988Z nl=2 -->

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

Sets the timeout duration for waiting when the communication task submission queue of the current AI CPU execution thread is full. When the communication task submission queue is full, task submission waits until space becomes available or the timeout expires.

## Function Prototype

```cpp
int32_t asccomm_thread_res_acquire_timeout(float timeOut)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| timeOut | Input | Timeout duration for waiting when the communication task submission queue is full, in seconds.<br>**0** means never time out; a value greater than 0 specifies the timeout duration, and a decimal value is truncated toward zero to an integer number of seconds.<br>The value cannot be negative, NaN, or greater than `UINT32_MAX`. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The timeout duration for waiting when the communication task submission queue is full is set successfully. |
| `ASCCOMM_E_PARA` | `timeOut` is negative, NaN, or greater than `UINT32_MAX`. |

## Usage Constraints

- This timeout duration applies only to communication task submissions subsequently triggered by the current AI CPU execution thread.
- If not set, the current implementation uses a default timeout duration of 1856 seconds.
- A timeout duration of 0 means never time out, which may cause a long wait. Use it with caution.
- A non-negative floating-point `timeOut` is allowed, but it is truncated toward zero to an integer number of seconds. `ASCCOMM_E_PARA` is returned for negative values, NaN, and values greater than `UINT32_MAX`.

## Call Example

```cpp
// timeout is the queue-full wait duration selected for this example. The actual value is determined by the caller based on the submission policy.
float timeout = 1800.0f;  // 1800 seconds; 0 means never time out
int32_t ret = asccomm_thread_res_acquire_timeout(timeout);
```
