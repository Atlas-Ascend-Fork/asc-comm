# asccomm_channel_drain_on_thread

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-04T10:19:35.871Z pushedAt=2026-11-09T06:47:54.997Z nl=2 -->

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

Waits for the submitted communication operations on the specified channel to complete.

This API is used to wait for the tasks submitted before the call to complete, and does not add order preservation constraints for tasks submitted after the call. If you need to guarantee the read/write operation order on the channel before and after the fence, call [asccomm_channel_fence_on_thread](asccomm_channel_fence_on_thread.md).

## Function Prototype

```cpp
int32_t asccomm_channel_drain_on_thread(ThreadHandle thread, ChannelHandle channel)
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
| `ASCCOMM_SUCCESS` (0) | The wait is processed successfully. Success is returned directly when the internal drain resources of the [RoCE protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection) or [UB protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection) are missing. |
| `ASCCOMM_E_PTR` | The thread or channel handle is 0, or the thread resource or local task cache pointer is null. |
| `ASCCOMM_E_PARA` | The thread/channel resource, communication task submission queue, or UB drain resource parameter is invalid. |
| `ASCCOMM_E_NOT_SUPPORT` | The channel protocol is neither a currently supported UB protocol nor the RoCE protocol. |
| `ASCCOMM_E_TIMEOUT` | A timeout occurs while waiting for space in the communication task submission queue when task submission is triggered. |
| `ASCCOMM_E_INTERNAL` | Internal execution fails, such as task generation, driver query or configuration, or local task cache operations. |

## Usage Constraints

- The current implementation supports UB protocols and the RoCE protocol; `ASCCOMM_E_NOT_SUPPORT` is returned for other protocols.
- When this API is called with the RoCE protocol, it returns success directly without performing additional waiting.
- UB protocols wait through internal completion notifications; when internal drain resources are missing, the wait is skipped and success is returned.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in by the control plane; channel is a valid ChannelHandle resource handle passed in by the control plane.
int32_t ret = asccomm_channel_drain_on_thread(thread, channel);
```
