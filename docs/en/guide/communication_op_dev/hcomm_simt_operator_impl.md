# Hcomm-based SIMT Communication Operator Development Guide

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-05T01:39:35.975Z pushedAt=2026-11-09T06:49:19.127Z nl=2 -->

## Overview

The AI Core-side SIMT point-to-point communication APIs of Hcomm select the communication protocol through the `AscendC::simt::Hcomm` template, providing unilateral data plane capabilities such as `WriteNbi`/`ReadNbi`/`WriteValueNbi`/`WriteWithNotifyNbi`/`AtomicFAA`/`AtomicCAS`, as well as the `Drain` completion-wait semantics. The SIMT (Single Instruction Multiple Threads) path is functionally similar to the SIMD (Single Instruction Multiple Data) path (`AscendC::Hcomm`, see [Hcomm-based Communication Operator Development Guide](hcomm_operator_impl.md)). The host-side control plane process is completely consistent, and the differences are concentrated on the kernel side: the APIs run in the SIMT execution space, `LocalTensor` and pipe synchronization primitives cannot be used, buffer addresses are resolved by channel buffer table index, and common APIs are driven by a single lane (a thread channel in the SIMT execution model, where multiple lanes execute in parallel within the same kernel) to drive the channel (batch APIs support group (a cooperative group, provided by `simt_api/cooperative_groups.h`) for multi-lane collaborative submission).

This guide uses AllGather as an example to introduce the complete development process of the SIMT communication operator based on Hcomm: the control plane (host-side) resource preparation is consistent with the SIMD path, so directly refer to [Hcomm-based Communication Operator Development Guide](hcomm_operator_impl.md), and only the steps with differences are expanded; the focus is on describing the usage differences between the kernel-side (data plane) communication APIs and the SIMD path. AllGather aggregates the local shard of each rank to all ranks in the communicator: each rank writes its local shard to the corresponding offset of the peer buffer of each remaining rank through `WriteNbi`. After the communication is complete, the buffers of all ranks hold the full data arranged by **rankId**.

## Supported Scope

| Item | Description |
| --- | --- |
| Communication protocol | `COMM_PROTOCOL_UB_CTP` |
| Communication engine | `COMM_ENGINE_AIV` |

<!-- npu="950" id1 -->
> [!NOTE] Note
> Only the Ascend 950PR&950DT products support implementing SIMT operators based on Hcomm. A single-device environment supports compilation verification only; actual running requires at least 2 NPUs.
<!-- end id1 -->

## Overall Process

Hcomm SIMT communication operator development is divided into two stages: control plane resource preparation and data plane communication execution.

The control plane process is completely consistent with the SIMD path. For the API descriptions and code examples of each step, see [Hcomm-based Communication Operator Development Guide](hcomm_operator_impl.md#control-plane-resource-preparation). Only steps 5 and 6 change from passing 64-bit addresses to passing buffer table indexes:

1. Initialize the operating environment.
2. Create a communicator.
3. Allocate memory and register communication memory.
4. Obtain the peer link and create a channel.
5. Obtain the peer registered memory table index.
6. Pass the channel and memory index to the kernel.
7. Destroy communication resources.

Data plane process:

1. Perform preprocessing.
2. Submit the communication task.
3. Wait for completion.

## Control Plane Resource Preparation

Except for the following two difference steps, the control plane process of the SIMT path is completely consistent with that of the SIMD path. For steps such as initializing the operating environment, creating the communicator, allocating memory and registering communication memory, obtaining the peer link and creating a channel, and destroying communication resources, see [Hcomm-based Communication Operator Development Guide](hcomm_operator_impl.md#control-plane-resource-preparation). For a complete SIMT process example, see [simt_hcomm_urma](../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md).

The difference lies in the address passing method: on the kernel side of the SIMT path, buffer addresses are resolved by channel buffer table index. Therefore, the following two steps pass the buffer table index instead of the 64-bit address.

### Obtaining the Peer Registered Memory Table Index

Query the communication memory registered by the peer through the channel, and match by tag to obtain the index of the peer memory in the remote memory table (to focus on the call process, the host-side example omits return value checks and error handling). For details, see [`HcclChannelGetRemoteMems`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetRemoteMems.md). The kernel side resolves the remote base address through this index via `AscendC::simt::RemoteBufferAddr`:

  ```cpp
  // Record the index of the peer registered memory in the remote memory table for the kernel side to resolve the base address.
  std::vector<uint32_t> remoteBufIdxs(channels.size(), 0U);
  for (uint32_t i = 0; i < channels.size(); i++) {
      uint32_t remoteMemoryCount = 0;
      CommMem* remoteMemories = nullptr;
      char** memoryTags = nullptr;
      HcclChannelGetRemoteMems(hcclComm, channels[i], &remoteMemoryCount, &remoteMemories, &memoryTags);

      for (uint32_t j = 0; j < remoteMemoryCount; j++) {
          if (memoryTags[j] != nullptr && strcmp(memoryTags[j], MEMORY_TAG) == 0) {
              remoteBufIdxs[i] = j;
              break;
          }
      }
  }
  ```

### Delivering Handles to the Kernel

Assemble each peer `channel` and the peer buffer table index `remoteBufIdxs` into a context shared between the host and the kernel (for the structure definition, see [Kernel-Side Communication Execution](#kernel-side-communication-execution); it must be defined in a header file shared between the host and the kernel), and then deliver it. The local buffer does not need to have its address delivered: the kernel side resolves it by the local buffer table index, where index `0` is the HcclBuffer of the communicator (the internal buffer registered when the channel is created by [`HcclChannelAcquire`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)), and explicitly registered memory starts from index `1` (consistent with the registration order of `HcclCommMemReg`).

  ```cpp
  void* devContext = nullptr;
  aclrtMalloc(&devContext, sizeof(AllGatherContext), ACL_MEM_MALLOC_HUGE_FIRST);

  AllGatherContext context = {};
  context.rankId = rank;
  context.peerNum = static_cast<uint32_t>(channels.size());
  for (uint32_t i = 0; i < channels.size(); i++) {
      context.channelHandles[i] = channels[i];
      context.remoteBufIdxs[i] = remoteBufIdxs[i];
  }
  aclrtMemcpy(devContext, sizeof(context), &context, sizeof(context), ACL_MEMCPY_HOST_TO_DEVICE);

  simt_all_gather_kernel<<<1, 0, stream>>>(devContext);
  ```

## Kernel-Side Communication Execution

The data plane API capabilities of the SIMT path and the SIMD path are basically aligned. The differences in kernel-side usage are summarized as follows (for the specific usage of each difference point, see the subsequent sections and [Precautions](#precautions)):

| Difference Point | SIMD Path (`AscendC::Hcomm`) | SIMT Path (`AscendC::simt::Hcomm`) |
| --- | --- | --- |
| Header file and template protocol | Header file `hcomm/hcomm.h`, template protocols `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` | Header file `hcomm/hcomm_simt.h`, template protocol `COMM_PROTOCOL_UB_CTP` |
| Kernel entry form | Directly expanded in the `__vector__ __global__ __aicore__` entry function | The `__global__ __vector__` entry calls `__simt_vf__` (VF, Vector Function) through `asc_vf_call` |
| Execution space | Can use `LocalTensor` and pipe synchronization primitives | Cannot use `LocalTensor` and pipe synchronization primitives |
| UB temporary workspace | The common API requires `Init` to bind a UB workspace of no less than 512 bytes. | No UB workspace required. `Init` always passes `nullptr` and `0`. |
| Buffer address | The host delivers the 64-bit addresses of the local and peer buffers, and the kernel uses them directly. | The host delivers the buffer table index, and the kernel resolves the base address through `LocalBufferAddr`/`RemoteBufferAddr`. |
| Call model | Directly called by the AI Core | The common API is driven by a single lane, and the remaining lanes return directly; the group batch API is collaboratively driven by multiple lanes in the group. |
| Deferred submission | Explicitly call `Commit` for unified submission | No `Commit` API; tasks with `commit = false` are carried out at task submission by subsequent tasks with `commit = true`, and tasks in the same batch must be submitted by the same lane. |
| Batch submission | Single-channel batch and multi-channel batch (`MultiChannelHandle`), batch workspace of at least 64 bytes. | Single-lane batch and multi-lane collaborative batch (group), batch workspace of at least 256 bytes |
| Channel sharing | Multiple AI Cores sharing the same channel require `Lock`/`Unlock` protection | Cannot drive the same channel simultaneously with the SIMD path `AscendC::Hcomm` |

The kernel entry function adopts the SIMT form: the `__global__ __vector__` entry calls the `__simt_vf__` function through `asc_vf_call`. The channel handle and the peer buffer table index are passed in as parameters through the `AllGatherContext` context, and the subsequent steps are expanded within this function. The protocol constants on the host side and the kernel side must be consistent (the SIMT path is `COMM_PROTOCOL_UB_CTP`):

  ```cpp
  #include "hcomm/hcomm_simt.h"
  #include "simt_api/asc_simt.h"
  #include "simt_api/cooperative_groups.h"

  constexpr uint32_t MAX_PEERS = 16U;   // Upper limit of the number of peers, which is adjusted according to the network scale.
  constexpr uint64_t SHARD_SIZE = 4096U;
  // Local buffer table index: index 0 is the HcclBuffer of the communicator, and explicitly registered memory starts from 1.
  constexpr uint32_t LOCAL_BUF_IDX = 1U;

  struct AllGatherContext {
      uint64_t channelHandles[MAX_PEERS];
      uint32_t remoteBufIdxs[MAX_PEERS];
      uint32_t rankId;
      uint32_t peerNum;  // Number of peers, that is, nranks - 1
  };

  __simt_vf__ inline void AllGatherVf(__gm__ AllGatherContext* ctx)
  {
      // The subsequent kernel-side communication execution tasks are expanded within this function.
  }

  extern "C" __global__ __vector__ void simt_all_gather_kernel(__gm__ void* context)
  {
      asc_vf_call<AllGatherVf>(dim3(1), reinterpret_cast<__gm__ AllGatherContext*>(context));
  }
  ```

Kernel-side communication APIs fall into two types: common APIs and batch APIs, both of which are called through the `AscendC::simt::Hcomm` object:

- Common API: directly takes `ChannelHandle` as the input parameter, submits a task on each call, and is called by a single lane.
- Batch API: takes a batch handle as the input parameter, assembles multiple tasks and submits them uniformly, and binds the UB workspace through `MakeBatchHandle`.

### Performing Preprocessing

The SIMT path does not require allocating a UB temporary workspace. It only requires creating an `AscendC::simt::Hcomm` object and calling `Init`. The two input parameters are unused, so pass in `nullptr` and `0` as fixed values. The communication task is initiated by a single lane, and the remaining lanes return directly:

  ```cpp
  // Single-lane driven channel: only lane 0 initiates communication, and the remaining lanes return directly.
  if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
      return;
  }

  // Create an Hcomm object and initialize it: no UB temporary workspace required.
  AscendC::simt::Hcomm<> hcomm;
  int32_t ret = hcomm.Init(nullptr, 0);
  ```

### Submitting Communication Tasks

AllGather: traverse all peer channels and write the local shard (at the `rankId * SHARD_SIZE` offset) to the same offset of each peer buffer. After communication completes, the buffer of each rank holds the shard of rank k at the `k * SHARD_SIZE` offset, which is the AllGather result:

  ```cpp
  for (uint32_t i = 0U; i < ctx->peerNum; i++) {
      uint64_t channel = ctx->channelHandles[i];
      // Resolve the local and peer registered memory base addresses by channel buffer table index.
      __gm__ uint8_t* localBuf = AscendC::simt::LocalBufferAddr(channel, LOCAL_BUF_IDX);
      __gm__ uint8_t* remoteBuf = AscendC::simt::RemoteBufferAddr(channel, ctx->remoteBufIdxs[i]);
      __gm__ uint8_t* remoteDst = remoteBuf + static_cast<uint64_t>(ctx->rankId) * SHARD_SIZE;
      __gm__ uint8_t* localSrc = localBuf + static_cast<uint64_t>(ctx->rankId) * SHARD_SIZE;
      ret = hcomm.WriteNbi(channel, remoteDst, localSrc, SHARD_SIZE);
  }
  ```

When there are many communication tasks, you can set `commit = false` for deferred submission to reduce submission overhead. For details, see [Deferred Submission and Batch Optimization](#deferred-submission-and-batch-optimization).

### Waiting for Completion

Call `Drain` channel by channel to block and wait for all tasks on that channel to complete:

  ```cpp
  for (uint32_t i = 0U; i < ctx->peerNum; i++) {
      ret = hcomm.Drain(ctx->channelHandles[i]);
  }
  ```

`Drain` only ensures that the tasks on the channels submitted by the local end are complete. AllGather requires that the result be complete only after all data exchange is finished: on the host side, after `aclrtSynchronizeStream`, you need to confirm that the kernels of all ranks are complete through inter-rank synchronization (such as the TCP control channel barrier), and then verify or use the AllGather result.

## Deferred Submission and Batch Optimization

### Deferred Submission

By default, a common API submits immediately on each call. Setting `commit = false` when submitting a task enables deferred submission, reducing submission overhead. The SIMT path has no `Commit` API: tasks with `commit = false` remain in the send queue and are carried out at task submission by any subsequent task with `commit = true` (whose doorbell release covers the producer indexes of the entire batch of tasks). Therefore, the same batch of deferred tasks must be submitted by a single lane:

  ```cpp
  // Prerequisite: channel is the local channel handle, dst is the destination GM address in the remote registered memory, src is the local source GM address, and len is the write length.
  // Deferred submission: only add the task without submitting it.
  ret = hcomm.WriteNbi<false>(channel, dst, src, len);
  // Carry out the entire batch of deferred tasks through this immediate submission.
  ret = hcomm.WriteNbi<true>(channel, dstLast, srcLast, lenLast);
  // Wait for all tasks on this channel to complete.
  ret = hcomm.Drain(channel);
  ```

### Batch Submission

When communication tasks need to be submitted in batch mode, use the batch API. The batch API adds tasks through the batch handle created by `MakeBatchHandle` and submits them uniformly through `BatchCommit`. Its UB workspace is bound by `MakeBatchHandle` (at least 256 bytes, that is, 128 bytes of shared context and 128 bytes of publisher DWQE (Direct WQE, a work queue element that directly writes to the hardware doorbell window) mirror; the workspace size does not grow with the group size) and is not passed in through `Init`. SIMT batch is divided into two forms: single-lane batch and multi-lane collaborative batch. Single-lane batch currently supports submitting only one task request per batch; when multiple tasks need to be submitted, add them one by one and submit them batch by batch. In multi-lane collaborative batch, each lane in the group adds one task per batch, and the number of tasks per batch equals the number of lanes.

Main constraints:

- For requests submitted through a batch handle, the target address must be within the remote registered memory of the channel to which the batch handle belongs, which is guaranteed by the caller.
- Requests appended within the same batch must use the same WQE width (that is, occupy the same number of task slots; for example, write-notify/atomic tasks occupy 2 task slots, and common write/read tasks occupy 1). Mixing widths is not supported.
- The same batch handle must use the same group (members, lane rank, and size remain unchanged) and the same UB workspace from creation to invalidation. The overload without a group is used for single-lane handles, and the handle with a group passed in is processed through the multi-lane collaborative path (even if size is 1). The group must be passed in for each task addition and `BatchCommit`.
- When the API for adding a task returns a failure, `BatchCommit` must not be called for that batch. Submitting an empty batch with no tasks added will fail.
- A channel can be exclusively occupied by only one alive **BatchHandle** at a time. During its lifetime, the common API cannot be used to submit tasks on that channel.

#### Single-Lane Batch

A single lane enqueues tasks through a batch handle and then publishes them uniformly. Currently, single-lane batch supports submitting only one task request per batch: after adding a task, call `BatchCommit` to submit it. To submit multiple tasks, add and submit them one by one, reusing the same handle across batches:

  ```cpp
  // Prepare the batch workspace: at least 256 bytes (128-byte shared context + 128-byte publisher DWQE mirror), with the start address aligned to 16 bytes.
  constexpr uint32_t WORKSPACE_BYTES = 256U;
  alignas(16) __ubuf__ uint8_t batchWorkspace[WORKSPACE_BYTES];

  auto batchHandle = hcomm.MakeBatchHandle(channel, batchWorkspace, WORKSPACE_BYTES);
  if (batchHandle.context == nullptr) {
      return;
  }

  // Prerequisite: remoteDst/localSrc/len are the destination/source GM addresses and length of the write task, prepared by the caller based on the business.
  // Add and submit only one task per batch, reusing the same handle across batches.
  ret = hcomm.WriteNbi(batchHandle, remoteDst, localSrc, len);
  ret = hcomm.BatchCommit(batchHandle);
  ret = hcomm.WriteNbi(batchHandle, remoteDst2, localSrc2, len2);
  ret = hcomm.BatchCommit(batchHandle);
  // Wait for the tasks submitted cumulatively through this batch handle to complete.
  ret = hcomm.Drain(batchHandle);
  ```

#### Multi-Lane Collaborative Batch

Each lane in the group constructs one request and adds it to the current batch. `BatchCommit` is a collaborative call (all lanes in the group must call it), and the publishing lane publishes the entire batch uniformly, with the number of tasks per batch equal to the number of lanes. The group is a cooperative group object provided by the header file `simt_api/cooperative_groups.h` (for example, obtain the current thread block group through `cooperative_groups::this_thread_block()`, and `__simt_vf__` functions must be issued in multi-lane form). The Hcomm object is lane-private; each lane in the group holds its own, and explicitly passes in the group each time a task is added and when `BatchCommit` is called:

  ```cpp
  // The group comes from simt_api/cooperative_groups.h and is constructed through APIs such as this_thread_block().
  auto group = cooperative_groups::this_thread_block();

  // Prerequisite: remoteDstForLane/localSrcForLane/len are the destination/source GM addresses and lengths of the task written by each lane.
  // The following calls are participated in by all lanes of the group, and each lane constructs its own request.
  auto batchHandle = hcomm.MakeBatchHandle(channel, batchWorkspace, WORKSPACE_BYTES, group);
  if (batchHandle.context == nullptr) {
      return;
  }
  ret = hcomm.WriteNbi(batchHandle, remoteDstForLane, localSrcForLane, len, group);
  ret = hcomm.BatchCommit(batchHandle, group);
  // After all lanes synchronize, one lane calls Drain. Drain does not receive the group.
  group.sync();
  if (group.thread_rank() == 0U) {
      ret = hcomm.Drain(batchHandle);
  }
  ```

For the complete process, see the samples [simt_hcomm_urma](../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md) (API functions), [simt_hcomm_urma_perftest](../../../../examples/aicore/hcomm/05_simt_hcomm_urma_perftest/README.md) (sending performance), and [simt_hcomm_urma_batch_perf](../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md) (batch submission method).

## Precautions

- The SIMT path supports only the `COMM_PROTOCOL_UB_CTP` protocol. The host-side channel protocol must be consistent with the `AscendC::simt::Hcomm` template protocol on the kernel side.
- The two input parameters of `Init` are unused. Pass `nullptr` and `0` fixedly, and no UB temporary workspace needs to be allocated.
- A channel must be driven by one lane: the common API is called by a single lane, and the same batch of tasks (including deferred tasks) must be submitted by the same lane, while the remaining lanes return directly.
- Buffer addresses are resolved by channel buffer table index: local table index 0 is the HcclBuffer of the communicator, and explicitly registered memory is arranged starting from index 1; the remote index is the index matched by tag in the memory table returned by `HcclChannelGetRemoteMems`. Do not directly pass the `memHandles` array index of `HcclCommMemReg`, otherwise it will be resolved to HcclBuffer.
- The SIMT path has no `Commit` API. Tasks with `commit = false` are carried out at task submission by subsequent tasks with `commit = true`.
- Batch submission: requests appended within the same batch must use the same WQE width; mixing widths is not supported. A single-lane batch currently supports submitting only one task request per batch. In a multi-lane collaborative batch, each lane in the group adds one task, and `BatchCommit` is called collaboratively by all lanes.
- SQ (Send Queue) occupancy: `WriteNbi`/`ReadNbi`/`WriteValueNbi` with deferred submission (`commit` is `false`) occupy 1 task slot; `WriteNbi`/`ReadNbi`/`WriteValueNbi` with immediate submission (`commit` is `true`) occupy 2 task slots; write-notify and atomic tasks occupy 2 task slots regardless of the submission mode. When `commit` is `false`, the sum of the slots occupied by accumulated deferred submission tasks and the slots occupied by the `commit`-`true` task that publishes the batch must not exceed the channel task submission capacity (SQ depth **sqDepth**). The accumulated tasks must be submitted by a `commit`-`true` task before the SQ is exhausted; otherwise, subsequent task submission will fail due to SQ overflow.
- The same channel cannot be driven simultaneously by the SIMT `AscendC::simt::Hcomm` and the SIMD `AscendC::Hcomm`.
- The data length of a single `WriteNbi`/`ReadNbi` must not exceed 256 MB; the data length `len` of the immediately submitted common API `WriteNbi`/`ReadNbi` must be at least 2 bytes.
