# MC2-based Communication Operator Development Guide

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-05T01:41:08.929Z pushedAt=2026-11-09T06:49:19.128Z nl=2 -->

## Overview

MC2 (Matrix Computation & Communication) is a concrete implementation of the proxy mode in Ascend C communication programming. On the host side, the ACL runtime is initialized, an HCCL communicator is created, and an MC2 communication resource context is allocated. The AI Core kernel acts as the communication client to submit communication tasks. AICPU_TS or CCU acts as the communication server to receive client requests, drive the communication path to complete data transfer, and publish the completion status to the client.

The core value of an MC2 operator is the overlap of communication and computation. After a communication task is submitted, the communication data transfer is executed by the AICPU_TS or CCU server, and the AI Core can continue to execute computations that have no dependency on the communication result of the current round. After the communication result is ready, the AI Core reads the communication output and executes the dependent computation, thereby overlapping part of the communication time with the computation time, reducing serial waiting, and improving the overall operator performance.

This guide describes the general development process of MC2 operators. It uses the AllGatherAdd sample as the communication API example. The host and kernel code is developed based on the AICPU_TS server path, with key differences of the CCU path supplemented. For the AICPU_TS sample, see [`02_allgather_add`](../../../../examples/aicpu/aicpu_builtin/02_allgather_add/README.md). For the CCU sample, see [`02_allgather_add`](../../../../examples/ccu/ccu_builtin/02_allgather_add/README.md). The computation part is represented by replaceable sample functions, which can be replaced with other vector or matrix computation logic.

## Supported Scope

| Item | Description |
| --- | --- |
| Communication client | AI Core |
| Communication server | AICPU_TS, CCU |

<!-- npu="950" id1 -->
> [!NOTE] Note
> The current MC2 path targets the Ascend 950PR&950DT products. In a single-device environment, only compilation verification is supported. Actual multi-rank communication running requires at least two NPUs. For the specific products, CANN versions, and API support scope, refer to the API reference of the current version.
<!-- end id1 -->

## Overall Process

The implementation of an MC2 operator consists of two parts: host-side resource preparation and kernel-side communication and computation execution.

Host-side process:

1. Initialize the ACL operating environment, bind the device, and create a task stream.
2. Create an HCCL communicator.
3. Allocate communication and computation buffers.
4. Configure MC2 communication parameters and allocate the communication resource context.
5. Start the communication server.
6. Start the AI Core client kernel that carries the communication context.
7. Synchronize the task stream, reclaim the results, and release resources.

Kernel-side process:

1. The AI Core client initializes the HCCL client.
2. Prepare the communication task. When asynchronous commit is used, call `Commit` after the communication source data is ready.
3. Execute computation based on data dependencies: the computation that generates the communication source data is executed before `Commit`; the computation unrelated to the communication output can be executed in parallel with communication; the computation that depends on the communication output is executed after the corresponding communication result is ready.
4. After communication and computation are complete, perform inter-core synchronization and call `Finalize` to end the communication collaboration with the server.

The client and server are two independent device-side execution entities. The host is responsible for task dispatch and synchronization, and does not directly execute device-side communication tasks.

## Host-Side Resource Preparation

### Initializing the ACL Operating Environment

When a process initializes the ACL runtime, each rank is bound to the corresponding device, and `streamAiv` is created for dispatching the AI Core client kernel.

  ```cpp
  uint32_t deviceId;   // Current device ID.

  ACL_CHECK(aclInit(nullptr));
  ACL_CHECK(aclrtSetDevice(deviceId));

  // Create the AI Core client task stream.
  aclrtStream streamAiv = nullptr;
  ACL_CHECK(aclrtCreateStream(&streamAiv));
  ```

### Creating an HCCL Communicator

`rankId` indicates the logical ID of the current rank in the HCCL communicator, and `rankSize` indicates the total number of ranks in the communicator. Each rank in the same communicator uses a unique `rankId`, and all ranks use the same `rankSize`.

The rank with `rankId == 0` is designated as the root rank. The root rank calls `HcclGetRootInfo` to generate the `HcclRootInfo` structure, which contains the information required for each rank to initialize the same HCCL communicator.

After all ranks in the communicator obtain the same `rootInfo` generated by the root rank, each rank calls `HcclCommInitRootInfo`. This API initializes the `HcclComm` handle of the current rank based on `rankSize`, `rankId`, and `rootInfo`. This handle is used for subsequent MC2 communication resource allocation.

The following code shows how each rank creates its own `HcclComm` handle after receiving the same `rootInfo`:

  ```cpp
  uint32_t rankId;        // ID of the current rank in the communicator
  uint32_t rankSize;      // Total number of ranks in the communicator
  HcclRootInfo rootInfo;  // Same information generated by the root rank and distributed to the current rank

  HcclComm comm = nullptr;
  HCCL_CHECK(HcclCommInitRootInfo(
      rankSize,
      &rootInfo,
      rankId,
      &comm));
  ```

### Allocating Communication and Computation Memory

An MC2 operator needs to allocate device GM buffers: the communication source buffer stores the data sent by the current rank; the communication destination buffer stores the communication result; and the computation input and communication source temporary buffers are accessed by the AI Core.

The buffer size is calculated based on the number of elements and the data type, and must satisfy the address alignment and accessible range requirements of the communication API. The communication source buffer cannot be overwritten before the communication is complete; the communication destination buffer cannot be read or overwritten before the corresponding result is written.

The following example uses FP32 data, where the communication source `tmpBuf` is generated from the computation result of `x1Buf + x2Buf`:

  ```cpp
  const uint64_t sendCount = 256;
  const uint64_t recvCount = sendCount * rankSize;
  const size_t sendSize = sendCount * sizeof(float);
  const size_t recvSize = recvCount * sizeof(float);

  void* x1Buf = nullptr;
  void* x2Buf = nullptr;
  void* tmpBuf = nullptr;
  void* recvBuf = nullptr;

  ACL_CHECK(aclrtMalloc(&x1Buf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ACL_CHECK(aclrtMalloc(&x2Buf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ACL_CHECK(aclrtMalloc(&tmpBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ACL_CHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));

  // Allocate the host input buffer and initialize it based on the operator input.
  void* hostBuf = nullptr;
  ACL_CHECK(aclrtMallocHost(&hostBuf, sendSize));
  float* hostInput = static_cast<float*>(hostBuf);
  for (uint64_t i = 0; i < sendCount; ++i) {
      hostInput[i] = static_cast<float>(rankId) + 1;
  }

  // In this example, x1 and x2 use the same input data.
  ACL_CHECK(aclrtMemcpy(x1Buf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
  ACL_CHECK(aclrtMemcpy(x2Buf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
  ACL_CHECK(aclrtFreeHost(hostBuf));
  ```

### Configuring MC2 Communication Resources

On the host side, you need to first create an MC2 communication parameter object, complete the communication configuration in the parameter object, and then allocate the communication resource context.

`ccArgs` points to the temporary host-side parameter object used when allocating the communication resource context `ccResCtx`. This object is created by `Mc2GetCcArgs`.

After creating the parameter object, set the communication execution engine through `Mc2SetCcCommEngine`, set the communication source and destination data types through `Mc2SetCcSrcDataType` and `Mc2SetCcDstDataType` respectively, and set the communication algorithm configuration through `Mc2SetCcAlgConfig`. These APIs write the configuration into the parameter object pointed to by `ccArgs` for later use when allocating the communication resource context.

The `Mc2AcquireCcResCtx` API is used to allocate the communication resource context of this communication. It takes the HCCL communication handle, the communication command type, and `ccArgs` as inputs, and outputs the communication resource context `ccResCtx` and its size `ccResCtxSize`. `ccResCtx` must be passed to the server startup API and the AI Core kernel later to access the communication resources corresponding to this communication.

After the communication resource context is allocated, call `Mc2FreeCcArgs` to release the temporary parameter object.

Example for the AICPU_TS path:

  ```cpp
  void* ccArgs = nullptr;
  void* ccResCtx = nullptr;
  uint32_t ccResCtxSize = 0;

  constexpr uint8_t kAicpuTsEngine = 2;  // OpExecuteConfig::AICPU_TS
  const uint8_t ccType = static_cast<uint8_t>(
      AscendC::HcclCMDType::HCCL_CMD_ALLGATHER);

  // Create the host-side MC2 communication parameter object.
  HCCL_CHECK(Mc2GetCcArgs(&ccArgs));

  // Set the communication execution engine to AICPU_TS.
  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, kAicpuTsEngine));

  // Set the communication source data type.
  HCCL_CHECK(Mc2SetCcSrcDataType(ccArgs, HCCL_DATA_TYPE_FP32));

  // Set the communication target data type.
  HCCL_CHECK(Mc2SetCcDstDataType(ccArgs, HCCL_DATA_TYPE_FP32));

  // Allocate the communication resource context based on the communication handle, communication command, and communication configuration.
  HCCL_CHECK(Mc2AcquireCcResCtx(
      comm, ccType, ccArgs, &ccResCtx, &ccResCtxSize));

  // Release the communication parameter object on the host side.
  HCCL_CHECK(Mc2FreeCcArgs(ccArgs));
  ```

When no algorithm configuration is set on the AICPU_TS path, the default algorithm configuration is used. On the CCU path, you need to explicitly set the communication algorithm configuration by calling `Mc2SetCcAlgConfig` before calling `Mc2AcquireCcResCtx`.

The following shows an example of the communication engine and algorithm configuration on the CCU path:

  ```cpp
  constexpr uint8_t kCcuSchedEngine = 6;  // OpExecuteConfig::CCU_SCHED
  const uint8_t ccType = static_cast<uint8_t>(
      AscendC::HcclCMDType::HCCL_CMD_ALLGATHER);
  char allGatherAlg[] = "CcuSchedAllGatherSoleMesh";

  // Set the communication execution engine to the CCU scheduling engine.
  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, kCcuSchedEngine));

  // Set the CCU AllGather algorithm configuration.
  HCCL_CHECK(Mc2SetCcAlgConfig(ccArgs, allGatherAlg));
  ```

Before allocating the communication resource context on the CCU path, you can call `CheckOpResSufficient` to precheck whether resources are sufficient. This API only performs a resource precheck and does not occupy communication resources. When `HCCL_SUCCESS` is returned, you can continue to call `Mc2AcquireCcResCtx`. When error code **1043** is returned, it indicates that CCU resources are temporarily insufficient, and you can select another communication algorithm. When any other non-zero error code is returned, it indicates a parameter error or an internal HCCL exception, and you need to handle the error first.

  ```cpp
  const HcclResult checkRet = CheckOpResSufficient(comm, ccType, ccArgs);
  if (checkRet != HCCL_SUCCESS) {
      // Perform degradation or exception handling based on the error code.
  }
  ```

### Starting the Server and Client

Within the same communicator, different AI CPU communication tasks share the AI CPU unfold stream. When HCCL communication tasks also exist in the communicator, the AI CPU communication tasks and HCCL communication tasks share the same unfold stream. The host can obtain the AI CPU unfold stream `unfoldStream` bound to the unfold thread of the communicator to dispatch AICPU_TS Server tasks. If notify is used for synchronization between the AI Core and AI CPU, ensure that concurrently executed communication tasks do not share the same notify.

On the host side, call `Mc2CcKernelLaunch` to start the server first, and then dispatch the AI Core client kernel to `streamAiv`. Both the server startup API and the client kernel use the communication resource context `ccResCtx` returned by `Mc2AcquireCcResCtx`.

The stream parameter of `Mc2CcKernelLaunch` is set based on the communication path:

- AICPU_TS path: pass `unfoldStream`, which is different from the `streamAiv` used by the client.
- CCU path: pass `nullptr`, and the client kernel is still dispatched to `streamAiv`.

The following uses the AICPU_TS path as an example. The client kernel receives the compute and communication buffer addresses, the communication resource context, and the communication scale as parameters:

  ```cpp
  // GetUnfoldThread and AcquireUnfoldStream are host-side helper functions in the AICPU_TS sample,
  // used to obtain the unfold thread of the communicator and its bound unfold stream. They are not public MC2 APIs.
  ThreadHandle unfoldThread = 0;
  aclrtStream unfoldStream = nullptr;
  HCCL_CHECK(GetUnfoldThread(comm, &unfoldThread));
  HCCL_CHECK(AcquireUnfoldStream(comm, unfoldThread, &unfoldStream));

  // Start the AICPU_TS server on the unfold stream managed by the communicator.
  HCCL_CHECK(Mc2CcKernelLaunch(unfoldStream, ccResCtx, ccResCtxSize));

  // Dispatch the AI Core client kernel.
  all_gather_add_kernel<<<1, nullptr, streamAiv>>>(
      x1Buf, x2Buf, tmpBuf, recvBuf, ccResCtx, sendCount);
  ```

### Synchronizing and Destroying Communication Resources

The AI Core client kernel consumes the communication result and performs computation. The communication result is written to GM by the server, and the computation result is written out by the client. The host side first synchronizes the client stream. Completion of the client kernel synchronization indicates that the communication result consumption and `Finalize` have been executed.

On the AICPU_TS path, the server exits after receiving `Finalize`. Therefore, after synchronizing the client stream, you also need to synchronize the server stream to confirm that the server task has exited:

  ```cpp
  ACL_CHECK(aclrtSynchronizeStream(streamAiv));
  ACL_CHECK(aclrtSynchronizeStream(unfoldStream));

  void* hostResult = nullptr;
  ACL_CHECK(aclrtMallocHost(&hostResult, recvSize));
  ACL_CHECK(aclrtMemcpy(
      hostResult, recvSize, recvBuf, recvSize,
      ACL_MEMCPY_DEVICE_TO_HOST));
  ```

Resources are released in dependency order: release the buffers, destroy the communicator, destroy the streams, reset the device, and finally perform ACL deinitialization.

`ccResCtx` is returned by `Mc2AcquireCcResCtx`. The current MC2 APIs do not provide a corresponding release APIs for users, so users do not directly release `ccResCtx`. Before destroying the HCCL communicator, you must first complete the client kernel synchronization and complete the server synchronization on the corresponding server path to ensure that all communication tasks in the communicator have ended.

On the AICPU_TS path, `unfoldStream` is the unfold stream bound to the communicator unfold thread. It is managed by the communicator and released together with the communicator. Users do not call `aclrtDestroyStream` to destroy this stream.

  ```cpp
  ACL_CHECK(aclrtFree(x1Buf));
  ACL_CHECK(aclrtFree(x2Buf));
  ACL_CHECK(aclrtFree(tmpBuf));
  ACL_CHECK(aclrtFree(recvBuf));
  ACL_CHECK(aclrtFreeHost(hostResult));

  HCCL_CHECK(HcclCommDestroy(comm));
  ACL_CHECK(aclrtDestroyStream(streamAiv));
  ACL_CHECK(aclrtResetDevice(deviceId));
  ACL_CHECK(aclFinalize());
  ```

## Kernel-Side Communication and Computation Execution

### Client Kernel Entry

The MC2 client runs on the AIV. This guide uses the vector computation scenario as an example. The kernel uses the `__vector__` entry, and the communication client and computation logic are written in the same kernel.

The kernel entry parameters are passed in by the host when the kernel is started, and typically include the communication source GM address, the communication destination GM address, the computation input/output GM addresses, the MC2 communication resource context, and the communication scale. `contextGM` uses the communication resource context address returned by `Mc2AcquireCcResCtx` on the host side, and the client accesses the communication resource through this context by calling `InitV2`.

The AICPU_TS path uses the AI CPU server type:

  ```cpp
  __global__ __vector__ void all_gather_add_kernel(
      __gm__ void* x1GM,
      __gm__ void* x2GM,
      __gm__ void* tmpGM,
      __gm__ void* recvGM,
      __gm__ void* contextGM,
      uint64_t sendCount)
  {
      if (g_coreType != AscendC::AIV) {
          return;
      }

      AscendC::InitSocState();

      AscendC::Hccl<AscendC::HcclServerType::HCCL_SERVER_TYPE_AICPU> hccl;
      hccl.InitV2((GM_ADDR)contextGM, nullptr);

      // Prepare the communication task, execute the computation that generates the source data, and then Commit.
  }
  ```

The CCU path uses the CCU Server type, and the kernel entry can be defined as follows:

  ```cpp
  AscendC::Hccl<AscendC::HcclServerType::HCCL_SERVER_TYPE_CCU> hccl;
  hccl.InitV2((GM_ADDR)contextGM);
  ```

### Preparing and Committing Communication Tasks

After the client completes `InitV2` initialization, it calls the HCCL communication API to create a communication task. The communication API is determined by the specific communication operation. This guide uses AllGather as an example.

The communication API returns `HcclHandle`, indicating that the communication request has been accepted. `AllGather<true>` commits immediately after preparation, while `AllGather<false>` only completes preparation. Regardless of which commit method is used, the returned handle does not indicate that the data in the destination GM is already available. The communication result must be confirmed through `Wait`.

The HCCL communication API supports two commit methods:

- Synchronous commit: uses `AllGather<true>`, which commits immediately after preparation is complete. It applies to scenarios where the communication source data is already ready before the communication API is called.
- Asynchronous commit: uses `AllGather<false>`, which does not commit during the preparation phase. After `HcclHandle` is returned, computation that generates the communication source data can continue. After the source data is ready, `Commit` is explicitly called.

The following example uses asynchronous commit. The communication source `tmpGM` is generated by the subsequent `AddCompute`:

  ```cpp
  AscendC::HcclHandle handleId =
      hccl.AllGather<false>(
          (GM_ADDR)tmpGM,
          (GM_ADDR)recvGM,
          sendCount,
          AscendC::HcclDataType::HCCL_DATA_TYPE_FP32,
          0);

  if (handleId < 0) {
      hccl.Finalize();
      return;
  }

  // Generate the communication source data: tmp = x1 + x2.
  AddCompute((GM_ADDR)x1GM, (GM_ADDR)x2GM, (GM_ADDR)tmpGM, sendCount);

  // After the communication source data is ready, publish the execution condition of this communication task to the server.
  hccl.Commit(handleId);
  ```

When using `<false>`, `Commit` must be called after the communication source data is ready. A communication task for which `Commit` is not called will not start data transmission.

The CCU example uses synchronous commit:

  ```cpp
  // sendBuf is the communication source GM, and yGM is the AllGather destination GM.
  AscendC::HcclHandle handleId =
      hccl.AllGather<true>(
          (GM_ADDR)sendBuf,
          (GM_ADDR)yGM,
          sendCount,
          AscendC::HcclDataType::HCCL_DATA_TYPE_FP32,
          0);
  ```

When performing other communication operations, replace it with the corresponding HCCL communication API, and set parameters such as the element count, data type, reduction type, offset, or repetition count as defined by that API.

### Executing Computation Based on Data Dependencies

Computation in an MC2 operator must be divided according to data dependencies:

- Computation that generates communication source data: can be executed after preparation and before commit.
- Computation independent of the communication output: can be executed after commit and before waiting.
- Computation that depends on the communication output: must be executed after the corresponding communication task or communication shard is complete.

`Wait` is used to confirm that the communication task corresponding to `HcclHandle` is complete. After `Wait` returns, the result written by the communication task to the destination GM can be read.

  ```cpp
  // 1. Prepare the communication task.
  AscendC::HcclHandle handleId =
      hccl.AllGather<false>(
          (GM_ADDR)tmpGM,
          (GM_ADDR)recvGM,
          sendCount,
          AscendC::HcclDataType::HCCL_DATA_TYPE_FP32,
          0);

  // 2. Execute the computation that generates communication source data.
  AddCompute((GM_ADDR)x1GM, (GM_ADDR)x2GM, (GM_ADDR)tmpGM, sendCount);

  // 3. Commit after the communication source data is ready.
  hccl.Commit(handleId);

  // 4. Execute the computation independent of the communication output.
  ComputeIndependent(...);

  // 5. Wait for the communication to complete.
  hccl.Wait(handleId);

  // 6. Consume the communication result and execute the computation that depends on the communication output.
  ConsumeCommunicationOutput((GM_ADDR)recvGM, ...);
  ```

`AddCompute`, `ComputeIndependent`, and `ConsumeCommunicationOutput` are the compute functions in the example. You can replace them with other Ascend C computation logic based on the operator functionality.

For multi-round or chunked communication, establish the data dependency of "source data ready, communication submitted, communication completed, result consumed, buffer reused" for each shard. Once a shard is complete, its result can be consumed. The destination GM corresponding to an incomplete shard must not be read in advance or handed over to the compute phase; otherwise, incomplete data may be read. The source buffer and destination buffer still in use by the communication must not be overwritten or reused in advance either; otherwise, read/write conflicts between communication and computation may occur.

### Ending Communication Collaboration

After the communication result consumption and computation are complete, call `AscendC::SyncAll<true>` to synchronize the participating cores. This synchronization is used to coordinate the execution progress of the participating cores and does not replace the `Wait` of the communication API. After the synchronization is complete, call `hccl.Finalize()` to end the communication collaboration between the client and the server.

  ```cpp
  // The current AIV has completed the Wait and result consumption of its own communication task.
  /* ... communication result consumption and computation ... */

  // Wait for all participating AIVs to complete the communication result consumption and computation of this round.
  AscendC::SyncAll<true>();

  // End the communication collaboration between the client and the server.
  hccl.Finalize();
  ```

If the client calls `Finalize` or exits before the communication result is fully consumed, the communication collaboration may end prematurely, and subsequent operations that access the communication context cannot be completed. Therefore, the client should enter the ending process only after the communication result consumption and computation are complete.

## Precautions

- On the AICPU_TS path, the server task needs to use an expansion dispatch stream that is independent of the AI Core client execution stream.
- When using asynchronous commit APIs such as `AllGather<false>`, `Commit` must be called after the communication source data is ready; a communication task that is not committed will not perform data transfer.
- A valid `HcclHandle` only indicates that the communication request has been accepted. Whether it has been committed is determined by the API template parameter, and the communication result must be confirmed by the corresponding `Wait` before it can be read.
- The destination GM cannot be consumed before `Wait` returns; the source GM cannot be overwritten or the destination GM reused before the communication is complete.
- When multiple AIVs participate in communication together, the communication call order, the number of `Wait` calls, the inter-core synchronization, and the `Finalize` timing of each participating core must be consistent.
- The communication engine, algorithm configuration, server startup method, and client template type differ between the AICPU_TS and CCU paths. When switching paths, you must modify both the host configuration and the client type on the kernel side.
