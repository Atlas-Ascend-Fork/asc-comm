# asccomm_write_on_thread

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-04T10:14:09.329Z pushedAt=2026-11-09T06:47:54.992Z nl=2 -->

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

Initiates a one-sided write on the specified channel to write `len` bytes of data pointed to by the local `src` into the memory pointed to by the remote `dst`. The API caller is the node where `src` resides.

## Function Prototype

```cpp
int32_t asccomm_write_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| channel | Input | Communication channel handle. After the control plane creates the channel resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ChannelHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| dst | Output | Remote destination address for receiving the written data. |
| src | Input | Local source address that provides the data to be written. |
| len | Input | Data length (in bytes). |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The write request is processed successfully. For the [UB protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection), it indicates that the write task has been submitted. For the [RoCE protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection), the write completion result is checked before returning. |
| `ASCCOMM_E_PTR` | `dst` or `src` is null, the thread or channel handle is 0, or the required resource address is null. |
| `ASCCOMM_E_PARA` | The thread or channel resource, communication context, or memory protection parameter is invalid. |
| `ASCCOMM_E_NOT_SUPPORT` | The channel protocol is neither the currently supported UB protocol nor the RoCE protocol. |
| `ASCCOMM_E_TIMEOUT` | The communication task submission queue or RoCE queue times out, or the RoCE completion event polling times out. |
| `ASCCOMM_E_REMOTE` | The RoCE completion event reports a remote communication error. |
| `ASCCOMM_E_INTERNAL` | Internal exception in tasks, queues, or other components. |

## Usage Constraints

- This API supports both the UB protocol and the RoCE protocol.
- `src` and `dst` should use the local and remote communication memory addresses corresponding to this channel.
- The `dst` address range must fall within the registered communication memory range on the remote end.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in by the control plane; channel is a valid ChannelHandle resource handle passed in by the control plane.
// src is the local communication memory address; dst is the remote communication memory address.
uint64_t len = 1024;
int32_t ret = asccomm_write_on_thread(thread, channel, dst, src, len);
```
