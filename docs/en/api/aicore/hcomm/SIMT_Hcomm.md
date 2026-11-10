# Hcomm

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:44:10.265Z pushedAt=2026-11-09T06:47:54.974Z nl=2 -->

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

Header file path: `"hcomm/hcomm_simt.h"`.

`AscendC::simt::Hcomm` is the corresponding API template of the SIMD (Single Instruction Multiple Data) path `AscendC::Hcomm` in the SIMT (Single Instruction Multiple Threads) execution space. It provides the same point-to-point communication primitives (**WriteNbi**, **ReadNbi**, and so on) and is suitable for SIMT kernel-side code where LocalTensor and pipe synchronization primitives are unavailable. The common API adds read, write, write-notify, and atomic operation tasks one by one through `channel`; the batch API adds tasks through the batch handle created by [MakeBatchHandle](./SIMT_MakeBatchHandle.md) and then submits them uniformly through [BatchCommit](./SIMT_BatchCommit.md). Batch tasks also support cooperative submission by multiple threads within a group through the cooperative group.

The underlying communication resources of a channel include SQ (Send Queue) and CQ (Completion Queue): when a communication task is added, the task description is written to SQ; after the task completes, a completion record (CQE, Completion Queue Entry) is generated and written to CQ, and [Drain](./SIMT_Drain.md) waits for task completion by polling CQ. A task is described by a WQE (Work Queue Element). One WQE occupies 1 to 2 task slots, and each task slot is 64 bytes, that is, one WQEBB (WQE Basic Block). The slot occupancy of the common API (the `channel` input parameter) depends on the submission mode: a write/read task submitted immediately (`commit` is `true`) occupies 2 task slots, and a write/read task submitted in a deferred manner (`commit` is `false`) occupies 1 task slot (when planning capacity, you must additionally account for 2 task slots for the subsequent immediate-submission task). The batch API (the `batchHandle` input parameter) does not provide a `commit` parameter and always adds tasks with deferred semantics: a write/read task occupies 1 task slot, and a write-notify/atomic task occupies 2. The task submission capacity is the SQ depth (sqDepth), which is determined when the host side creates the channel resources.

The typical usage flow is as follows:

1. Create an **Hcomm** object and call [Init](./SIMT_Init.md) to complete initialization.
2. Submit communication tasks asynchronously through the corresponding APIs.
3. Call [Drain](./SIMT_Drain.md) to block and wait for the communication tasks to complete.

## Function Prototype

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UB_CTP>
class Hcomm;
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `commProtocol` | Communication protocol type. Currently only `COMM_PROTOCOL_UB_CTP` is supported, and the default value is `COMM_PROTOCOL_UB_CTP`. The selected protocol determines the corresponding communication path. |

## Usage Constraints

**Protocol constraints**

- The SIMT implementation currently supports only the `COMM_PROTOCOL_UB_CTP` protocol and does not support `COMM_PROTOCOL_ROCE`.

**General constraints**

- Before calling the communication API, the caller must complete initialization of the communication channel resources.
- The **Hcomm** object is private to a lane (thread): the common API is called by a single thread; in the batch group cooperative overload, one thread plans the SQ (Send Queue) range, each thread in the group writes to its own task slot, and [BatchCommit](./SIMT_BatchCommit.md) publishes them uniformly after these writes are ready.
- The SIMT path has no independent **Commit** API. Tasks with `commit` set to `false` remain in the send queue until a subsequent task with `commit` set to `true` triggers submission.
- The same channel cannot be driven simultaneously by this class and the SIMD `AscendC::Hcomm`.

## Call Example

The following shows an example of calling the **Hcomm** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample of the common API, see [04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md); for a complete sample of batch submission, see [06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md).

Common API:

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;

__simt_callee__ void SimtWriteOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBuffer)
{
    // Create and initialize the SIMT Hcomm object. The COMM_PROTOCOL_UB_CTP protocol requires no temporary workspace.
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Write the local localBuffer data to the remote remoteBuffer.
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // Block and wait for the write task to complete.
    hcomm.Drain(channel);
}
```

Batch API:

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Batch workspace size: no less than 256 bytes, and it does not grow with the number of batch tasks.
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchWriteOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // Declare the batch UB workspace, with the start address aligned to 16 bytes.
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // Create a batch handle and bind it to the batch workspace.
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // Add the tasks of the current batch and commit them.
    hcomm.WriteNbi(batchHandle, remoteBase, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // Reuse the batch handle to add and commit the next batch of tasks.
    hcomm.WriteNbi(batchHandle, remoteBase + BYTES, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // Wait for all completion records generated by the cumulatively committed tasks.
    hcomm.Drain(batchHandle);
}
```
