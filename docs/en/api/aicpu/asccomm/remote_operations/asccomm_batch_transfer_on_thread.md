# asccomm_batch_transfer_on_thread

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-04T10:12:30.561Z pushedAt=2026-11-09T06:47:54.991Z nl=2 -->

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

Asynchronously submits a group of transfer tasks on the specified thread and channel. Each transfer task is described by `AsccommBatchTransferDesc`, which is defined in `"aicpu/hcomm/asccomm_res_defs.h"`, and supports operation types such as one-sided write, one-sided read, write reduction, reduction read, write with notification, reduction write with notification, and notification record.

## Function Prototype

```cpp
int32_t asccomm_batch_transfer_on_thread(ThreadHandle thread, ChannelHandle channel,
    const AsccommBatchTransferDesc* transferDescs, uint32_t transferDescNum)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| channel | Input | Communication channel handle. After the control plane creates the channel resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ChannelHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| transferDescs | Input | Batch transfer descriptor array. Each element in the array describes a transfer task, and the tasks are submitted sequentially in array order. The type is `AsccommBatchTransferDesc`, defined in `"aicpu/hcomm/asccomm_res_defs.h"`. |
| transferDescNum | Input | Number of descriptors, which must be greater than 0. |

### transferDescs Description

`AsccommBatchTransferDesc` is defined in `"aicpu/hcomm/asccomm_res_defs.h"` and is used to describe a batch transfer task. This structure contains reserved fields and a union. The caller should first initialize the entire descriptor with `{}`, and then set the corresponding union member according to `transType`.

| Field | Description |
| --- | --- |
| `transType` | Transfer task type. For values, see the following table. |
| `reserved[4]` | Reserved field. Not read by the current implementation. It is recommended to keep it as **0**. |
| `transferInfo` | Union. Select the corresponding member according to `transType` to store task parameters. |

The `raws` in `transferInfo` is a raw byte view. The current API does not read `raws`. The caller should use the specific task members in the following table to set parameters.

| `transType` | `transferInfo` Member | Field Description |
| --- | --- | --- |
| `ASCCOMM_TRANSFER_TYPE_WRITE` | `write` | `len` is the number of bytes to transfer; `src` is the local source address; `dst` is the remote destination address. |
| `ASCCOMM_TRANSFER_TYPE_READ` | `read` | `len` is the number of bytes to transfer; `src` is the remote source address; `dst` is the local destination address. |
| `ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE` | `reduce` | `count` is the number of elements; `src` is the local source address; `dst` is the remote destination address; `dataType` is the data type; `reduceOp` is the reduction operation. |
| `ASCCOMM_TRANSFER_TYPE_READ_REDUCE` | `reduce` | `count` is the number of elements; `src` is the remote source address; `dst` is the local destination address; `dataType` is the data type; `reduceOp` is the reduction operation. |
| `ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY` | `writeWithNotify` | `len` is the number of bytes to transfer; `src` is the local source address; `dst` is the remote destination address; `notifyIdx` is the remote Notify index. |
| `ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY` | `writeReduceWithNotify` | `count` is the number of elements; `src` is the local source address; `dst` is the remote destination address; `dataType` is the data type; `reduceOp` is the reduction operation; `notifyIdx` is the remote Notify index. |
| `ASCCOMM_TRANSFER_TYPE_NOTIFY_RECORD` | `notifyRecord` | `notifyIdx` is the remote Notify index. |
| `ASCCOMM_TRANSFER_TYPE_INVALID` | Not applicable | Not accepted by the current implementation. Returns `ASCCOMM_E_PARA`. |

### Reduction Descriptor Support

The supported ranges of `dataType` and `reduceOp` are the same for `ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE`, `ASCCOMM_TRANSFER_TYPE_READ_REDUCE`, and `ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY`.

| Dimension | Supported Values |
| --- | --- |
| `dataType` | `ASCCOMM_DATA_TYPE_INT8`, `ASCCOMM_DATA_TYPE_INT16`, `ASCCOMM_DATA_TYPE_INT32`, `ASCCOMM_DATA_TYPE_UINT8`, `ASCCOMM_DATA_TYPE_UINT16`, `ASCCOMM_DATA_TYPE_UINT32`, `ASCCOMM_DATA_TYPE_FP16`, `ASCCOMM_DATA_TYPE_FP32`, `ASCCOMM_DATA_TYPE_BFP16` |
| `reduceOp` | `ASCCOMM_REDUCE_SUM`, `ASCCOMM_REDUCE_MAX`, `ASCCOMM_REDUCE_MIN` |

The return values for unsupported types are as follows:

| Scenario | Return Value |
| --- | --- |
| `ASCCOMM_REDUCE_PROD` | `ASCCOMM_E_INTERNAL` |
| `ASCCOMM_DATA_TYPE_INT64`, `ASCCOMM_DATA_TYPE_UINT64`, `ASCCOMM_DATA_TYPE_FP64`, `ASCCOMM_DATA_TYPE_INT128` | `ASCCOMM_E_INTERNAL` |
| `ASCCOMM_DATA_TYPE_HIF8`, `ASCCOMM_DATA_TYPE_FP8E4M3`, `ASCCOMM_DATA_TYPE_FP8E5M2`, `ASCCOMM_DATA_TYPE_FP8E8M0`, `ASCCOMM_DATA_TYPE_RESERVED` | `ASCCOMM_E_PARA` |
| `ASCCOMM_REDUCE_RESERVED` | `ASCCOMM_E_PARA` |
| `count * sizeof(dataType)` overflow | `ASCCOMM_E_PARA` |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS`(0) | The transfer task is processed and submitted successfully. |
| `ASCCOMM_E_PARA` | The parameter or communication resource configuration is invalid. |
| `ASCCOMM_E_PTR` | The handle, pointer, or task address is null or invalid. |
| `ASCCOMM_E_NOT_SUPPORT` | The channel uses a protocol that is not supported by the current implementation. |
| `ASCCOMM_E_TIMEOUT` | The communication queue has insufficient space during immediate submission and the wait times out. |
| `ASCCOMM_E_INTERNAL` | An internal execution exception occurs. |

## Usage Constraints

- `transferDescs` cannot be **NULL**, and `transferDescNum` must be greater than 0.
- The current implementation supports only the [UB protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection); the [RoCE protocol](../../../../guide/programming_model/communication_pattern_selection.md#communication-protocol-classification-and-selection) returns `ASCCOMM_E_NOT_SUPPORT`.
- Except for `ASCCOMM_TRANSFER_TYPE_NOTIFY_RECORD`, the source address and destination address of each descriptor cannot be **NULL**, and the address range must fall within the communication memory range registered for the channel.
- For transfer tasks with notification and notification record tasks, `notifyIdx` must correspond to a valid remote Notify resource of the channel.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in from the control plane; channel is a valid ChannelHandle resource handle passed in from the control plane.
// localBuffer/localBuffer2 are the local registered communication memory source addresses, corresponding to the src field of the descriptor; remoteBuffer/remoteBuffer2 are the remote registered communication memory destination addresses, corresponding to the dst field of the descriptor.
AsccommBatchTransferDesc descs[2] = {};

// Descriptor 1: one-sided write.
descs[0].transType = ASCCOMM_TRANSFER_TYPE_WRITE;
descs[0].transferInfo.write.len = 4096;
descs[0].transferInfo.write.dst = remoteBuffer;    // Remote address.
descs[0].transferInfo.write.src = localBuffer;     // Local address.

// Descriptor 2: one-sided write (with notification).
descs[1].transType = ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY;
descs[1].transferInfo.writeWithNotify.len = 8192;
descs[1].transferInfo.writeWithNotify.dst = remoteBuffer2;    // Remote address.
descs[1].transferInfo.writeWithNotify.src = localBuffer2;     // Local address.
descs[1].transferInfo.writeWithNotify.notifyIdx = 0;          // Notification index.

// Submit the batch transfer. Error handling is omitted here. Check ret in actual calls.
int32_t ret = asccomm_batch_transfer_on_thread(thread, channel, descs, 2);
(void)ret;
```
