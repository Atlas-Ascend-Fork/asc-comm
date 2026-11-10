# asccomm_channel_fence_on_thread

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:19:44.599Z pushedAt=2026-11-09T06:47:54.997Z nl=2 -->

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

Sets a fence flag on the specified channel to ensure that channel read/write operations before the fence complete before channel read/write operations after the fence.

## Function Prototype

```cpp
int32_t asccomm_channel_fence_on_thread(ThreadHandle thread, ChannelHandle channel)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| channel | Input | Communication channel handle. After the control plane creates the channel resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ChannelHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The fence flag is set successfully. |
| `ASCCOMM_E_PTR` | The thread or channel handle is 0, or the thread resource pointer is null. |
| `ASCCOMM_E_PARA` | The thread/channel resource or communication context parameter is invalid. |

## Usage Constraints

- This API only sets the fence flag of the channel. The flag is consumed by the next related transfer task and is automatically cleared after consumption.
- The channel resource must already have an available communication context. If the context is missing, `ASCCOMM_E_PARA` is returned.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in from the control plane; channel is a valid ChannelHandle resource handle passed in from the control plane.
int32_t ret = asccomm_channel_fence_on_thread(thread, channel);
```
