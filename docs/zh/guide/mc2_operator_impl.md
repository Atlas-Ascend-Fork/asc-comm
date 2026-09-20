# 基于MC2的通算算子开发指南

## 概述

MC2（Matrix Computation & Communication）是代理模式在Ascend C通信编程中的一类具体实现。Host侧初始化ACL运行时，创建HCCL通信域，并申请MC2通信资源上下文；AICore Kernel作为通信Client提交通信任务；AICPU_TS或CCU作为通信Server接收Client请求，驱动通信通路完成数据传输，并向Client发布完成状态。

MC2通算算子的核心价值是通信与计算重叠。通信任务提交后，通信数据传输由AICPU_TS或CCU服务端执行，AICore可以继续执行与本轮通信结果没有依赖的计算；待通信结果就绪后，AICore再读取通信输出并执行依赖计算，从而将部分通信时间与计算时间重叠，减少串行等待，提高算子整体性能。

本指南介绍MC2通算算子的通用开发流程。文中以AllGatherAdd样例作为通信接口示例，Host和Kernel代码按AICPU_TS Server路径展开，并补充CCU路径的关键差异。计算部分使用可替换的示例函数表示，可替换为其他的Vector、Matrix计算逻辑。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 通信Client | AICore |
| 通信Server | AICPU_TS、CCU |

<!-- npu="950" id1 -->
> [!NOTE]说明
> 当前MC2路径面向Ascend 950PR&950DT系列产品。单卡环境仅支持编译验证，实际多Rank通信运行需要至少2张NPU。具体产品、CANN版本和接口支持范围以当前版本API参考为准。
<!-- end id1 -->

## 总体流程

MC2通算算子实现分为Host侧资源准备和Kernel侧通信与计算执行两部分。

Host侧流程：

1. 初始化ACL运行环境，绑定Device，创建任务流。
2. 创建HCCL通信域。
3. 分配通信和计算缓冲区。
4. 配置MC2通信参数并申请通信资源上下文。
5. 启动通信Server。
6. 启动携带通信上下文的AICore Client Kernel。
7. 同步任务流，回收结果并释放资源。

Kernel侧流程：

1. AICore Client初始化HCCL客户端。
2. Prepare通信任务；使用异步Commit时，在通信源数据就绪后调用 `Commit`。
3. 按数据依赖执行计算：生成通信源数据的计算在 `Commit` 前执行；与通信输出无关的计算可以和通信并行执行；依赖通信输出的计算，在对应通信结果就绪后执行。
4. 通信与计算完成后，执行核间同步，并调用 `Finalize` 结束与Server的通信协作。

Client和Server是两个独立的Device侧执行实体。Host负责任务下发和同步，本身不直接执行Device侧通信任务。

AICPU_TS路径下，Host通过 `Mc2CcKernelLaunch` 将Server任务下发到独立的AICPU stream。CCU路径下，Host通过 `Mc2CcKernelLaunch(nullptr, ...)` 启动Server，Client Kernel下发到AICore stream。

## Host侧资源准备

### 初始化ACL运行环境

每个Rank先绑定对应的Device，再由Host通过ACL在该Device上创建用于下发Device任务的stream。

AICPU_TS路径需要创建两个stream：

- `aicpuStream`：下发AICPU_TS Server任务。
- `streamAiv`：下发AICore Client Kernel。

Server任务会等待AICore Client的通信请求。如果Server和Client放在同一个stream中串行执行，Server等待请求时可能阻塞后续Client Kernel的下发，因此两者使用独立stream。

CCU路径下，Client Kernel下发到 `streamAiv`；Server由 `Mc2CcKernelLaunch(nullptr, ...)` 启动，不使用独立的Host stream参数。

  ```cpp
  uint32_t deviceId;   // 当前Device编号
  uint32_t rankId;     // 当前Rank在通信域中的逻辑编号
  uint32_t rankSize;   // Rank总数

  ACL_CHECK(aclInit(nullptr));
  ACL_CHECK(aclrtSetDevice(deviceId));

  aclrtStream aicpuStream = nullptr;
  aclrtStream streamAiv = nullptr;
  ACL_CHECK(aclrtCreateStream(&aicpuStream));
  ACL_CHECK(aclrtCreateStream(&streamAiv));
  ```

### 创建HCCL通信域

`rankId` 表示当前Rank在HCCL通信域中的逻辑编号，`rankSize` 表示通信域中的Rank总数。同一通信域中的每个Rank使用唯一的 `rankId`，所有Rank使用相同的 `rankSize`。

约定 `rankId == 0` 的Rank为Root Rank。Root Rank调用 `HcclGetRootInfo` 生成 `HcclRootInfo` 结构体。该结构体包含各Rank初始化同一个HCCL通信域所需的信息。

通信域中的所有Rank获得Root Rank生成的同一份 `rootInfo` 后，分别调用 `HcclCommInitRootInfo`。该接口根据 `rankSize`、`rankId` 和 `rootInfo` 初始化当前Rank的 `HcclComm` 句柄。后续MC2通信资源申请使用这个句柄。

以下代码展示各Rank已经收到同一份 `rootInfo` 后，创建当前Rank `HcclComm` 句柄的过程：

  ```cpp
  uint32_t rankId;        // 当前Rank在通信域中的编号
  uint32_t rankSize;      // 通信域中的Rank总数
  HcclRootInfo rootInfo;  // Root Rank生成并分发给当前Rank的同一份信息

  HcclComm comm = nullptr;
  HCCL_CHECK(HcclCommInitRootInfo(
      rankSize,
      &rootInfo,
      rankId,
      &comm));
  ```

### 分配通信和计算内存

MC2算子需要分配Device GM缓冲区：通信源缓冲区保存当前Rank发送的数据；通信目的缓冲区保存通信结果；计算输入和通信源临时缓冲区由AICore访问。

缓冲区大小根据元素数量和数据类型计算，并满足通信接口的地址对齐和可访问范围。通信源缓冲区在通信完成前不能覆盖；通信目的缓冲区在对应结果写入完成前不能读取或覆盖。

以下示例使用FP32数据，通信源 `tmpBuf` 由 `x1Buf + x2Buf` 的计算结果生成：

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

  // 申请Host输入缓冲区，并按算子输入初始化。
  void* hostBuf = nullptr;
  ACL_CHECK(aclrtMallocHost(&hostBuf, sendSize));
  float* hostInput = static_cast<float*>(hostBuf);
  for (uint64_t i = 0; i < sendCount; ++i) {
      hostInput[i] = static_cast<float>(rankId) + 1;
  }

  // 示例中x1与x2使用同一份输入数据。
  ACL_CHECK(aclrtMemcpy(x1Buf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
  ACL_CHECK(aclrtMemcpy(x2Buf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
  ACL_CHECK(aclrtFreeHost(hostBuf));
  ```

### 配置MC2通信资源

Host侧需要先创建MC2通信参数对象，在参数对象中完成通信配置，再申请通信资源上下文。

`ccArgs` 用于保存MC2通信配置，由 `Mc2GetCcArgs` 创建，由 `Mc2FreeCcArgs` 释放。创建后，通过 `Mc2SetCc*` 系列接口设置通信引擎、数据类型和算法配置。

`Mc2AcquireCcResCtx` 接口用于申请本次通信的通信资源上下文，以HCCL通信句柄、通信命令类型和 `ccArgs` 为输入，输出通信资源上下文 `ccResCtx` 及其大小 `ccResCtxSize`。`ccResCtx` 后续需要传给Server启动接口和AICore Kernel，用于访问本次通信所对应的通信资源。

AICPU_TS路径示例：

  ```cpp
  void* ccArgs = nullptr;
  void* ccResCtx = nullptr;
  uint32_t ccResCtxSize = 0;

  constexpr uint8_t kAicpuTsEngine = 2;  // OpExecuteConfig::AICPU_TS
  const uint8_t ccType = static_cast<uint8_t>(
      AscendC::HcclCMDType::HCCL_CMD_ALLGATHER);

  // 创建Host侧MC2通信参数对象。
  HCCL_CHECK(Mc2GetCcArgs(&ccArgs));

  // 设置通信执行引擎为AICPU_TS。
  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, kAicpuTsEngine));

  // 设置通信源数据类型。
  HCCL_CHECK(Mc2SetCcSrcDataType(ccArgs, HCCL_DATA_TYPE_FP32));

  // 设置通信目标数据类型。
  HCCL_CHECK(Mc2SetCcDstDataType(ccArgs, HCCL_DATA_TYPE_FP32));

  // 根据通信句柄、通信命令和通信配置，申请通信资源上下文。
  HCCL_CHECK(Mc2AcquireCcResCtx(
      comm, ccType, ccArgs, &ccResCtx, &ccResCtxSize));

  // 释放Host侧通信参数对象。
  HCCL_CHECK(Mc2FreeCcArgs(ccArgs));
  ```

需要显式指定算法时，在调用`Mc2AcquireCcResCtx`前调用`Mc2SetCcAlgConfig`设置算法配置；未调用该接口时，使用当前接口的默认算法配置。

CCU路径将通信执行引擎设置为CCU调度引擎，并显式设置AllGather算法配置：

  ```cpp
  constexpr uint8_t kCcuSchedEngine = 6;  // OpExecuteConfig::CCU_SCHED
  const uint8_t ccType = static_cast<uint8_t>(
      AscendC::HcclCMDType::HCCL_CMD_ALLGATHER);
  char allGatherAlg[] = "CcuSchedAllGatherSoleMesh";

  // 设置通信执行引擎为CCU调度引擎。
  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, kCcuSchedEngine));

  // 设置CCU AllGather算法配置。
  HCCL_CHECK(Mc2SetCcAlgConfig(ccArgs, allGatherAlg));
  ```

### 启动Server和Client

AICPU_TS路径下，Host侧先下发Server任务，再下发AICore Client Kernel。

Server任务通过 `Mc2CcKernelLaunch` 下发到 `aicpuStream`。

  ```cpp
  Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize);
  ```

AICore Client Kernel下发到 `streamAiv`。Kernel参数包括通信源/目的GM地址、计算输入/输出缓冲区、`ccResCtx` 和通信规模等运行时参数：

  ```cpp
  all_gather_add_kernel<<<1, nullptr, streamAiv>>>(
      x1Buf, x2Buf, tmpBuf, recvBuf, ccResCtx, sendCount);
  ```

Server任务启动后会等待AICore Client提交通信请求。因此，Server任务需要先于Client Kernel下发，并且两者使用不同的stream。

CCU路径下，Host调用 `Mc2CcKernelLaunch(nullptr, ...)` 启动Server，再将AICore Client Kernel下发到 `streamAiv`。以下示例使用通信源缓冲区 `tmpBuf`，通信结果和计算结果分别写入 `recvBuf` 和 `computeBuf`：

  ```cpp
  Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize);

  vector_add<<<1, nullptr, streamAiv>>>(
      tmpBuf, recvBuf, computeBuf, ccResCtx,
      sendCount, recvCount);
  ```

### 同步和销毁通信资源

通信和计算结果由AICore Client Kernel写出，Host侧先同步Client stream。Client Kernel同步完成表示通信结果消费和 `Finalize` 已经执行完成。

AICPU_TS路径下，Server收到 `Finalize` 后退出，因此同步Client stream后还需要同步Server stream，确认Server任务已经退出：

  ```cpp
  ACL_CHECK(aclrtSynchronizeStream(streamAiv));
  ACL_CHECK(aclrtSynchronizeStream(aicpuStream));

  void* hostResult = nullptr;
  ACL_CHECK(aclrtMallocHost(&hostResult, recvSize));
  ACL_CHECK(aclrtMemcpy(
      hostResult, recvSize, recvBuf, recvSize,
      ACL_MEMCPY_DEVICE_TO_HOST));
  ```

资源释放顺序与创建顺序相反：释放缓冲区，销毁通信域，销毁stream，重置Device，最后执行ACL去初始化。

`ccResCtx` 由 `Mc2AcquireCcResCtx` 返回，当前MC2接口没有单独提供对应的用户释放接口，用户不直接释放 `ccResCtx`。销毁HCCL通信域前，必须先完成Client Kernel同步，并按对应Server路径完成Server同步，确保通信域内的通信任务已经全部结束。

  ```cpp
  ACL_CHECK(aclrtFree(x1Buf));
  ACL_CHECK(aclrtFree(x2Buf));
  ACL_CHECK(aclrtFree(tmpBuf));
  ACL_CHECK(aclrtFree(recvBuf));
  ACL_CHECK(aclrtFreeHost(hostResult));

  HCCL_CHECK(HcclCommDestroy(comm));
  ACL_CHECK(aclrtDestroyStream(streamAiv));
  ACL_CHECK(aclrtDestroyStream(aicpuStream));
  ACL_CHECK(aclrtResetDevice(deviceId));
  ACL_CHECK(aclFinalize());
  ```

## Kernel侧通信与计算执行

### Client Kernel入口

MC2 Client运行在AIV上。本指南以Vector计算场景为例，Kernel使用 `__vector__` 入口，通信Client和计算逻辑写在同一个Kernel中。

Kernel入口参数由Host启动Kernel时传入，通常包括通信源GM地址、通信目的GM地址、计算输入/输出GM地址、MC2通信资源上下文和通信规模等。`contextGM` 使用Host侧 `Mc2AcquireCcResCtx` 返回的通信资源上下文地址，Client通过 `InitV2` 使用该上下文访问通信资源。

AICPU_TS路径使用AICPU Server类型：

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

      // Prepare通信任务，执行生成通信源数据的计算，再Commit。
  }
  ```

CCU路径使用CCU Server类型，Kernel入口可按以下形式定义：

  ```cpp
  AscendC::Hccl<AscendC::HcclServerType::HCCL_SERVER_TYPE_CCU> hccl;
  hccl.InitV2((GM_ADDR)contextGM);
  ```

### Prepare和Commit通信任务

Client完成 `InitV2` 初始化后，调用HCCL通信接口创建通信任务。通信接口由具体通信操作决定，本指南以AllGather为例。

通信接口返回 `HcclHandle`，表示通信请求已被接受。`AllGather<true>` 在Prepare后立即Commit，`AllGather<false>` 仅完成Prepare。无论使用哪种Commit方式，返回句柄都不表示目的GM中的数据已经可用，通信结果必须经过 `Wait` 确认。

HCCL通信接口支持两种Commit方式：

- 同步Commit：使用 `AllGather<true>`，Prepare完成后立即Commit，适用于通信源数据在调用通信接口前已经就绪的场景。
- 异步Commit：使用 `AllGather<false>`，Prepare阶段不Commit，返回 `HcclHandle` 后可以继续执行生成通信源数据的计算，源数据就绪后显式调用 `Commit`。

以下示例使用异步Commit。通信源 `tmpGM` 由后续 `AddCompute` 生成：

  ```cpp
  AscendC::HcclHandle handleId =
      hccl.AllGather<false>(
          (GM_ADDR)tmpGM,
          (GM_ADDR)recvGM,
          sendCount,
          AscendC::HcclDataType::HCCL_DATA_TYPE_FP32,
          0);

  if (handleId < 0) {
      return;
  }

  // 生成通信源数据：tmp = x1 + x2。
  AddCompute((GM_ADDR)x1GM, (GM_ADDR)x2GM, (GM_ADDR)tmpGM, sendCount);

  // 通信源数据就绪后，向Server发布该通信任务的执行条件。
  hccl.Commit(handleId);
  ```

使用 `<false>` 时，必须在通信源数据就绪后调用 `Commit`。未调用 `Commit` 的通信任务不会开始执行数据传输。

CCU示例中使用同步Commit：

  ```cpp
  AscendC::HcclHandle handleId =
      hccl.AllGather<true>(
          (GM_ADDR)sendGM,
          (GM_ADDR)recvGM,
          sendCount,
          AscendC::HcclDataType::HCCL_DATA_TYPE_FP32,
          0);
  ```

执行其他通信操作时，应替换为对应的HCCL通信接口，并按该接口定义设置元素个数、数据类型、归约类型、偏移或重复次数等参数。

### 按数据依赖执行计算

MC2算子中的计算需要按数据依赖划分：

- 生成通信源数据的计算：可以在Prepare之后、Commit之前执行。
- 与通信输出无关的计算：可以在Commit之后、Wait之前执行。
- 依赖通信输出的计算：必须在对应通信任务或通信分片完成后执行。

`Wait` 用于确认对应 `HcclHandle` 的通信任务已经完成。`Wait` 返回后，目的GM中该通信任务写入的结果可以读取。

  ```cpp
  // 1. Prepare通信任务。
  AscendC::HcclHandle handleId =
      hccl.AllGather<false>(
          (GM_ADDR)tmpGM,
          (GM_ADDR)recvGM,
          sendCount,
          AscendC::HcclDataType::HCCL_DATA_TYPE_FP32,
          0);

  // 2. 执行生成通信源数据的计算。
  AddCompute((GM_ADDR)x1GM, (GM_ADDR)x2GM, (GM_ADDR)tmpGM, sendCount);

  // 3. 通信源数据就绪后Commit。
  hccl.Commit(handleId);

  // 4. 执行与通信输出无关的计算。
  ComputeIndependent(...);

  // 5. 等待通信完成。
  hccl.Wait(handleId);

  // 6. 消费通信结果，执行依赖通信输出的计算。
  ConsumeCommunicationOutput((GM_ADDR)recvGM, ...);
  ```

`AddCompute`、`ComputeIndependent` 和 `ConsumeCommunicationOutput` 是示例中的计算函数，可以按算子功能替换为其他Ascend C计算逻辑。

多轮或分块通信时，应为每个分片建立“源数据就绪、通信提交、通信完成、结果消费、缓冲区复用”的数据依赖。某个分片完成后即可消费该分片；未完成分片对应的目的GM不能提前读取或交给计算阶段，否则可能读取到不完整的数据。通信仍在使用的源缓冲区和目的缓冲区也不能提前覆盖或复用，否则会产生通信与计算之间的读写冲突。

### 核间同步和结束通信协作

当多个AIV共同参与同一个MC2通信任务时，参与通信的AIV需要按照相同顺序提交通信请求，并对对应通信任务完成相应的 `Wait`。多轮通信时，各参与核需要处理匹配的通信轮次，保持对共享通信上下文的访问顺序一致。

每个参与核完成自己负责的通信结果消费和计算后，调用 `AscendC::SyncAll<true>` 等待其他参与核。该同步用于协调参与核的执行进度，不替代通信接口的 `Wait`。只有所有参与核都完成本轮通信确认和结果消费后，才能调用 `hccl.Finalize()` 结束Client与Server之间的通信协作。

  ```cpp
  // 当前AIV已完成自身通信任务的Wait和结果消费。
  /* ... communication result consumption and computation ... */

  // 等待所有参与AIV完成本轮通信结果消费和计算。
  AscendC::SyncAll<true>();

  // 结束Client与Server之间的通信协作。
  hccl.Finalize();
  ```

如果某个参与核在其他核仍然提交通信任务、等待通信结果或消费通信输出时提前调用 `Finalize` 或退出，可能导致通信协作提前结束，其他核继续访问通信上下文时无法完成。因此，所有参与核都应在本地通信和计算完成后，再统一进入核间同步和结束流程。

## 注意事项

- AICPU_TS路径下，Server会阻塞等待Client请求，Server和Client必须使用独立stream。
- 使用`AllGather<false>`等异步Commit接口时，必须在通信源数据就绪后调用`Commit`；未Commit的通信任务不会执行数据传输。
- `HcclHandle`有效只代表通信请求已被接受，是否已经Commit由接口模板参数决定，通信结果必须经过对应`Wait`确认后才能读取。
- `Wait`返回前不能消费目的GM；通信完成前不能覆盖源GM或复用目的GM。
- 多AIV共同参与通信时，各参与核的通信调用顺序、`Wait`次数、核间同步和`Finalize`时机必须一致。
- AICPU_TS与CCU路径的通信引擎、算法配置、Server启动方式和Client模板类型不同，切换路径时必须同时修改Host配置与Kernel侧Client类型。
