# asccomm_channel_notify_record_on_thread

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-04T10:20:55.783Z pushedAt=2026-11-09T06:47:54.999Z nl=2 -->

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

Sends a synchronization signal to the remote Notify of a specified channel. It is mainly used in scenarios where both ends of a channel wait for synchronization.

## Function Prototype

```cpp
int32_t asccomm_channel_notify_record_on_thread(ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| channel | Input | Communication channel handle. After the control plane creates the channel resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ChannelHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| remoteNotifyIdx | Input | Remote Notify index. The value range is `[0, remoteNotifyNum)`, where `remoteNotifyNum` is the number of remote Notify entries in the channel resource. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The notification task is processed successfully. For the [UB protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection), it indicates that the task has been generated. For the [RoCE protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection), the completion result of the notification write operation is checked before returning. |
| `ASCCOMM_E_PTR` | The thread or channel handle is 0, or the communication resource or memory protection pointer is null. |
| `ASCCOMM_E_PARA` | The thread/channel resource, communication context, Notify, or memory protection parameter is invalid. |
| `ASCCOMM_E_NOT_SUPPORT` | The channel protocol is neither the currently supported UB protocol nor the RoCE protocol. |
| `ASCCOMM_E_TIMEOUT` | The communication task submission queue or RoCE queue times out while waiting, or the RoCE completion event polling times out. |
| `ASCCOMM_E_REMOTE` | The RoCE completion event reports a remote communication error. |
| `ASCCOMM_E_INTERNAL` | An internal exception occurs in task generation, queues, or the local task cache. |

## Usage Constraints

- The current implementation supports the UB protocol and the RoCE protocol. `ASCCOMM_E_NOT_SUPPORT` is returned for other protocols.
- `remoteNotifyIdx` must be less than the number of remote Notify entries in the channel, and the corresponding Notify resource type and memory protection parameters must be correct.
- The timing of notification task submission is consistent with the submission policy of the thread resource. In normal mode, the task can be submitted immediately. In batch mode or when the cache is full, it is submitted according to the current implementation.
- This API must be used together with [asccomm_channel_notify_wait_on_thread](asccomm_channel_notify_wait_on_thread.md).

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in by the control plane; channel is a valid ChannelHandle resource handle passed in by the control plane; remoteNotifyIdx is the remote Notify index.
uint32_t remoteNotifyIdx = 0;
int32_t ret = asccomm_channel_notify_record_on_thread(thread, channel, remoteNotifyIdx);
```
