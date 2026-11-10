# Hcomm

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:33:22.970Z pushedAt=2026-11-09T06:47:54.967Z nl=2 -->

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

Header file path: `"hcomm/hcomm.h"`.

`AscendC::Hcomm` is a point-to-point communication API template on the AI Core side, providing two call chains: the common API and the batch API. The common API adds individual read, write, write-with-notify, atomic operation, and reduce-write tasks through `ChannelHandle`. This call chain requires [Init](./Init.md) to provide a temporary workspace and uses [Commit](./Commit.md) and [Drain](./Drain.md) to control task submission and completion waiting. The **BatchHandle** batch API supports adding multiple tasks and then submitting them together: the `COMM_PROTOCOL_UB_CTP` protocol supports single-channel or multi-channel batching (in multi-channel mode, multiple logical channels share the same batch handle and underlying communication resources), while the `COMM_PROTOCOL_ROCE` protocol supports only single-channel batching. This call chain does not depend on `Init`; its UB (Unified Buffer) workspace is provided by [MakeBatchHandle](./MakeBatchHandle.md), and completion waiting uses the batch [Drain](./Drain.md).

The underlying communication resources of a channel include the SQ (Send Queue) and the CQ (Completion Queue): when a communication task is added, the task description is written to the SQ; after the task completes, a completion record (CQE) is generated and written to the CQ, and [Drain](./Drain.md) waits for task completion by polling the CQ. In this document, a task slot corresponds to a 64-byte WQE (Work Queue Element) block in the SQ, the task submission capacity is the SQ depth (sqDepth), and the completion record capacity is the CQ depth (cqDepth), all of which are determined when the host side creates the channel resources.

## Function Prototype

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UB_CTP>
class Hcomm;
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `commProtocol` | Communication protocol type. `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UB_CTP` are supported, with a default value of `COMM_PROTOCOL_UB_CTP`. The selected protocol determines the corresponding communication path. |

The batch API supports the `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols. In single-channel mode, use the handle returned by [MakeBatchHandle](./MakeBatchHandle.md) directly to add tasks; when tasks need to be submitted to multiple channels, create a batch handle for each channel separately. Multi-channel mode is supported only by the `COMM_PROTOCOL_UB_CTP` protocol, where multiple logical channels share the same batch handle and underlying communication resources. Use [GetHandleRef](./GetHandleRef.md) to select a logical channel and add tasks, and use the multi-channel batch handle returned by `MakeBatchHandle` to execute [BatchCommit](./BatchCommit.md) and the batch [Drain](./Drain.md). `COMM_PROTOCOL_ROCE` does not support multi-channel mode.

## Return Value

APIs that return a status code typically use `0` to indicate success and a non-`0` value to indicate failure. Under the `COMM_PROTOCOL_UB_CTP` protocol, `Drain` also directly returns the detailed error code from completion record polling to distinguish polling timeout from completion status errors. `MakeBatchHandle` directly returns the batch handle and does not return a status code. For the specific return value of each API, see the corresponding API documentation.

## Usage Constraints

**Protocol constraints**

**Table 2** Protocol support for common APIs

| API | `COMM_PROTOCOL_UB_CTP` | `COMM_PROTOCOL_ROCE` |
| --- | --- | --- |
| `Init` | Supported | Supported |
| `Lock` | Supported | Supported |
| `Unlock` | Supported | Supported |
| `ReadNbi` | Supported | Supported |
| `WriteNbi` | Supported | Supported |
| `WriteValueNbi` | Supported | Not supported |
| `WriteWithNotifyNbi` | Supported | Not supported |
| `AtomicFAA` | Supported | Not supported |
| `AtomicCAS` | Supported | Not supported |
| `WriteReduceNbi` | Supported | Not supported |
| `Commit` | Supported | Supported |
| `Drain` | Supported | Supported |

**Table 3** Protocol support for batch API

| API | `COMM_PROTOCOL_UB_CTP` | `COMM_PROTOCOL_ROCE` |
| --- | --- | --- |
| `MakeBatchHandle` | Supported | Supported |
| `GetHandleRef` | Supported | Supported |
| `WriteNbi` | Supported | Supported |
| `ReadNbi` | Supported | Supported |
| `WriteWithNotifyNbi` | Supported | Not supported |
| `BatchCommit` | Supported | Supported |
| `Drain` | Supported | Supported |

The `ChannelHandle` passed in must point to a channel entity that matches the selected protocol.

**General constraints**

- Before calling a communication API, the caller must complete initialization of the communication channel resources.
- While using a **BatchHandle**, you must exclusively hold its associated channel resources. You cannot mix common APIs or use multiple **BatchHandle** calls concurrently. For detailed constraints, see [MakeBatchHandle](./MakeBatchHandle.md) and [Drain](./Drain.md).

For detailed constraints on each API, such as workspace requirements, task submission capacity, and completion record configuration, see the corresponding API documentation, for example [Init](./Init.md), [WriteNbi](./WriteNbi.md), and [Commit](./Commit.md).

For the host-side handle creation API in multi-channel mode, see [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md). This API provides two overloads: HCCL communicator and Hcomm endpoint.

## Call Example

The following shows an example of calling the Hcomm API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample of the common API, see [01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md). For a complete sample of the batch API, see [02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md).

Common API:

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UB temporary workspace size: must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Write the local localBuffer data to the remote remoteBuffer.
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // Block until the write task completes.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Number of write tasks added in a single batch.
constexpr uint32_t TASK_NUM = 4U;
// Batch workspace size: divided into 64-byte task slots, it must accommodate the slots of the entire batch (the total number of slots must be less than the task submission capacity of the channel).
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Declare the batch UB workspace. The batch API does not depend on Init. alignas(32) ensures that the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // Create an Hcomm object.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // Construct the batch workspace LocalTensor from the start address of the UB workspace.
    AscendC::LocalTensor<uint8_t> batchBuffer(
        AscendC::TPosition::VECOUT,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

    // Create a single-channel batch handle and bind it to the batch workspace. remoteBuffer is used to select the remote registered memory.
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // Add TASK_NUM write tasks to the current batch in sequence, writing to different remote offsets. The tasks are first accumulated in the batch workspace and have not been submitted yet.
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.WriteNbi(batchHandle, remoteBuffer + i * BYTES, localBuffer, BYTES);
    }

    // Submit all tasks accumulated in the current batch at once.
    hcomm.BatchCommit(batchHandle);

    // Block and wait for all completion records generated by the entire batch of tasks.
    hcomm.Drain(batchHandle);
}
```
