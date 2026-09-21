# 基于MC2的通算算子开发指南

## 概述

MC2（Massive Communication Computing）是代理模式在Ascend C通信编程中的一类具体实现。Host侧初始化ACL运行时，创建HCCL通信域，并申请MC2通信资源上下文；AICore Kernel作为通信Client提交通信任务；AICPU_TS或CCU任务作为通信Server接收Client请求，驱动通信通路完成数据传输，并向Client发布完成状态。

MC2通算算子的核心价值是通信与计算重叠。通信任务提交后，通信数据传输由AICPU_TS或CCU服务端执行，AICore可以继续执行与本轮通信结果没有依赖的计算；待通信结果就绪后，AICore再读取通信输出并执行依赖计算，从而将部分通信时间与计算时间重叠，减少串行等待，提高算子整体性能。

本指南介绍MC2通算算子的通用开发流程。文中以AllGather作为通信接口示例，Host和Kernel代码按AICPU_TS Server路径展开；
【占位待确认】CCU同为MC2支持的Server路径，可能在通信引擎、算法配置、Server启动接口和Client类型等方面存在差异，具体组合应以当前版本API参考和对应样例为准。计算部分使用可替换的占位函数表示，实际算子可以替换为其他集合通信或点对点通信接口，并将计算阶段替换为Vector、Matrix或其他Ascend C计算逻辑。

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

MC2通算算子实现分为控制面资源准备和数据面通信与计算执行两阶段。

控制面流程：

1. 初始化ACL运行环境，绑定Device，创建任务流。
2. 创建HCCL通信域。
3. 分配通信输入、通信输出和计算缓冲区。
4. 配置MC2通信参数并申请通信资源上下文。
5. 启动AICPU_TS或CCU通信Server。
6. 启动携带通信上下文的AICore Client Kernel。
7. 同步任务流，回收结果并释放资源。

数据面流程：

1. AICore Client初始化HCCL客户端。
2. 在通信输入就绪后提交通信任务。
3. 按数据依赖执行计算：与通信输出无关的计算可以和通信并行执行；依赖通信输出的计算，在对应通信结果就绪后读取通信输出并执行。
4. 通信与计算完成后，执行核间同步，并结束与Server的通信协作。

```text
Host控制面:
  HCCL domain -> MC2 args -> OpResCtx -> Server launch -> Client launch

AICore Client:
  InitV2 -> submit communication -> execute by data dependency -> consume result -> end

AICPU_TS/CCU Server:
  receive request -> drive communication path -> publish completion
```

Client和Server是两个独立的Device侧执行实体。Host负责任务下发和同步，本身不直接执行Device侧通信任务。AICPU_TS路径下，Host通过`Mc2AicpuKernelLaunch`启动Server，并将Server与Client下发到不同stream。
【占位待确认】CCU路径的Server启动接口、Client类型和上下文传递方式需结合当前版本API参考及对应样例确定，本文不将单个CCU样例的配置推广为通用规则。

## 控制面资源准备

### 初始化ACL运行环境

每个Rank先绑定对应的Device，再由Host通过ACL在该Device上创建用于下发Device任务的stream。AICPU_TS路径下，Server和AICore Client应分别使用独立的Server stream和Client stream，避免Server等待Client请求时与Client形成同一stream内的顺序依赖。

【占位待确认】CCU路径下的stream或任务队列要求、Server启动方式以及Client与Server的调度关系，需结合当前版本接口和对应样例进一步确认。

  ```cpp
  uint32_t rankId;    // 当前Rank ID
  uint32_t rankSize;  // Rank总数

  aclInit(nullptr);
  ACL_CHECK(aclrtSetDevice(rankId));

  aclrtStream serverStream = nullptr;
  aclrtStream clientStream = nullptr;
  ACL_CHECK(aclrtCreateStream(&serverStream));
  ACL_CHECK(aclrtCreateStream(&clientStream));
  ```

### 创建HCCL通信域

`rankId`表示当前Rank在通信域中的逻辑编号，`rankSize`表示通信域中的Rank总数。同一通信域中的每个Rank使用唯一的`rankId`，所有Rank使用相同的`rankSize`。

约定`rankId == 0`的Rank为Root Rank。Root Rank调用`HcclGetRootInfo`生成`HcclRootInfo`结构体。该结构体包含各Rank初始化同一个HCCL通信域所需的信息。

通信域中的所有Rank获得Root Rank生成的同一份`rootInfo`后，分别调用`HcclCommInitRootInfo`。该接口根据`rankSize`、`rankId`和`rootInfo`初始化当前Rank的`HcclComm`句柄。后续HCCL通信操作和MC2通信资源申请都使用这个句柄。

通信域初始化完成后，调用`HcclGetCommName`获取当前`HcclComm`对应的通信域名称。在MC2参数配置阶段，将该名称传给`Mc2SetCcGroupName`，使MC2通信资源关联到前面创建的HCCL通信域。

代码示例：

  ```cpp
  uint32_t rankId;    // 当前Rank在通信域中的逻辑编号
  uint32_t rankSize;  // 通信域中的Rank总数

  HcclRootInfo rootInfo;

  if (rankId == 0) {
      HCCL_CHECK(HcclGetRootInfo(&rootInfo));
  }

  // 将Root Rank生成的同一份rootInfo提供给通信域中的所有Rank。

  HcclComm comm = nullptr;
  HCCL_CHECK(HcclCommInitRootInfo(
      rankSize,
      &rootInfo,
      rankId,
      &comm));

  char commName[COMM_NAME_MAX_LENGTH] = {0};
  HCCL_CHECK(HcclGetCommName(comm, commName));

  HCCL_CHECK(Mc2SetCcGroupName(ccArgs, commName));
  ```

### 分配通信和计算内存

为通信和计算分别分配Device GM缓冲区。通信输入缓冲区保存当前Rank待发送的数据，通信输出缓冲区保存通信操作写入的结果；这两个缓冲区都需要传递给通信接口。计算输入缓冲区保存计算阶段需要读取的数据，计算输出缓冲区保存计算结果，均由AICore访问。

分配缓冲区时，根据通信和计算实际使用的元素数量、数据类型计算所需字节数，并满足通信接口要求的地址对齐和可访问范围。通信任务执行期间，通信输入缓冲区中的源数据必须保持不变，通信输出缓冲区在通信完成前不能被计算阶段读取或覆盖。只有对应通信结果写入完成后，计算阶段才能读取通信输出并继续执行依赖该结果的计算。

  ```cpp
  const uint64_t sendCount = 1024;              // 示例值，元素个数
  const uint64_t recvCount = sendCount * rankSize;
  const size_t sendSize = sendCount * sizeof(half);
  const size_t recvSize = recvCount * sizeof(half);

  void* sendBuf = nullptr;
  void* recvBuf = nullptr;
  void* computeInBuf = nullptr;
  void* computeOutBuf = nullptr;

  ACL_CHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_FIRST));
  ACL_CHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_FIRST));
  ACL_CHECK(aclrtMalloc(&computeInBuf, recvSize, ACL_MEM_MALLOC_HUGE_FIRST));
  ACL_CHECK(aclrtMalloc(&computeOutBuf, recvSize, ACL_MEM_MALLOC_HUGE_FIRST));

  // 将Host侧输入拷贝到Device GM；输出缓冲区可按需清零。
  ACL_CHECK(aclrtMemcpy(sendBuf, sendSize, hostSendData, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
  ACL_CHECK(aclrtMemcpy(computeInBuf, recvSize, hostComputeData, recvSize, ACL_MEMCPY_HOST_TO_DEVICE));
  ```

### 配置MC2通信资源

Host侧需要先创建MC2通信参数对象，在参数对象中完成通信配置，再申请通信资源上下文。

`ccArgs`用于保存MC2通信配置，由`Mc2GetCcArgs`创建，由`Mc2FreeCcArgs`释放。创建后，通过`Mc2SetCc*`系列接口设置通信域、通信算法、数据类型和通信执行引擎。

`Mc2AcquireCcResCtx`接口用于申请本次通信的通信资源上下文，以HCCL通信句柄、通信命令类型和`ccArgs`为输入，输出通信资源上下文`opResCtx`及其大小`opResCtxSize`。`opResCtx`后续需要传给Server启动接口和AICore Kernel，用于访问本次通信所对应的通信资源。

  ```cpp
  // AICPU_TS引擎编号，具体优先使用当前头文件中的枚举定义。
  constexpr uint8_t MC2_COMM_ENGINE_AICPU_TS = 2;
  constexpr uint8_t ccType = static_cast<uint8_t>(
      AscendC::HcclCMDType::HCCL_CMD_ALLGATHER);
  char algConfig[] = "AllGather=level0:doublering";

  void* ccArgs = nullptr;
  void* opResCtx = nullptr;
  uint32_t opResCtxSize = 0;

  // 创建Host侧MC2通信参数对象。
  HCCL_CHECK(Mc2GetCcArgs(&ccArgs));

  // 绑定通信资源所属的HCCL通信域。
  HCCL_CHECK(Mc2SetCcGroupName(ccArgs, commName));

  // 设置通信算法。
  HCCL_CHECK(Mc2SetCcAlgConfig(ccArgs, algConfig));

  // 设置通信源数据类型。
  HCCL_CHECK(Mc2SetCcSrcDataType(ccArgs, HCCL_DATA_TYPE_FP16));

  // 设置通信目标数据类型。
  HCCL_CHECK(Mc2SetCcDstDataType(ccArgs, HCCL_DATA_TYPE_FP16));

  // 设置通信执行引擎。
  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, MC2_COMM_ENGINE_AICPU_TS));

  // 根据通信句柄、通信命令和通信配置，申请通信资源上下文。
  HCCL_CHECK(Mc2AcquireCcResCtx(
      comm, ccType, ccArgs, &opResCtx, &opResCtxSize));

  // 释放Host侧通信参数对象。
  HCCL_CHECK(Mc2FreeCcArgs(ccArgs));
  ```

### 启动Server和Client

AICPU_TS路径下，Host侧应先下发Server任务，再下发AICore Client Kernel，并且两者使用不同的stream。

Server任务通过`Mc2AicpuKernelLaunch`下发到`serverStream`。接口参数中的`opResCtx`必须使用`Mc2AcquireCcResCtx`返回的通信资源上下文。

  ```cpp
  // blockDim为0时，接口内部按1个block处理。
  HCCL_CHECK(Mc2AicpuKernelLaunch(
      0, serverStream, opResCtx, opResCtxSize));
  ```

AICore Client Kernel下发到`clientStream`。Kernel参数包括通信源/目的GM地址、计算输入/输出缓冲区、`opResCtx`和通信规模等运行时参数。

  ```cpp
  mc2_operator_custom<<<blockDim, nullptr, clientStream>>>(
      sendBuf, recvBuf, computeInBuf, computeOutBuf, opResCtx,
      sendCount, HCCL_DATA_TYPE_FP16);
  ```

Server任务启动后会等待AICore Client提交通信请求。因此，Server任务需要先于Client Kernel下发；同时，Server任务和Client Kernel不能放在同一个stream中串行执行，否则Server在等待Client请求时可能阻塞后续Client Kernel的下发。使用独立的`serverStream`和`clientStream`后，Server可以先启动并等待请求，Client Kernel随后提交通信任务并继续执行。

【占位待确认】CCU路径下，Host侧将通信引擎和算法配置设置为CCU路径支持的配置，调用`Mc2CcKernelLaunch`启动Server。AICore Client使用`Hccl<AscendC::HcclServerType::HCCL_SERVER_TYPE_CCU>`，并在调用`InitV2`时传入Host侧获取的同一个通信资源上下文。

  ```cpp
  // CCU调度引擎编号，具体优先使用当前头文件中的枚举定义。
  constexpr uint8_t MC2_COMM_ENGINE_CCU_SCHED = 6;
  char algConfig[] = "CcuSchedAllGatherSoleMesh";

  // 配置CCU路径的通信引擎和算法，并申请通信资源上下文。
  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, MC2_COMM_ENGINE_CCU_SCHED));
  HCCL_CHECK(Mc2SetCcAlgConfig(ccArgs, algConfig));
  HCCL_CHECK(Mc2AcquireCcResCtx(
      comm, ccType, ccArgs, &ccResCtx, &ccResCtxSize));
  HCCL_CHECK(Mc2FreeCcArgs(ccArgs));

  // 启动CCU Server。
  Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize);
  ```

### 同步和销毁通信资源

通信和计算结果由AICore Client Kernel写出，Host侧先同步Client stream。Client Kernel同步完成表示通信结果消费和`Finalize`已经执行完成。

AICPU_TS路径下，Server收到`Finalize`后退出，因此同步Client stream后还需要同步Server stream，确认Server任务已经退出。

  ```cpp
  ACL_CHECK(aclrtSynchronizeStream(clientStream));
  ACL_CHECK(aclrtSynchronizeStream(serverStream));

  // 拷回通信结果或计算结果。
  ACL_CHECK(aclrtMemcpy(hostResult, recvSize, computeOutBuf, recvSize, ACL_MEMCPY_DEVICE_TO_HOST));
  ```

【占位待确认】CCU路径下，Server由`Mc2CcKernelLaunch`启动，Host侧同步Client stream。

资源释放顺序与创建顺序相反：释放Device和Host缓冲区，销毁通信域，销毁stream，重置Device，最后执行ACL去初始化。`opResCtx`由`Mc2AcquireCcResCtx`返回，当前MC2接口没有单独提供对应的用户释放接口，用户不直接释放`opResCtx`；但销毁HCCL通信域前，必须先完成Client Kernel同步，并按对应Server路径完成Server同步，确保通信域内的通信任务已经全部结束。

  ```cpp
  ACL_CHECK(aclrtFree(sendBuf));
  ACL_CHECK(aclrtFree(recvBuf));
  ACL_CHECK(aclrtFree(computeInBuf));
  ACL_CHECK(aclrtFree(computeOutBuf));
  ACL_CHECK(aclrtFreeHost(hostSendData));
  ACL_CHECK(aclrtFreeHost(hostComputeData));
  ACL_CHECK(aclrtFreeHost(hostResult));

  HCCL_CHECK(HcclCommDestroy(comm));
  ACL_CHECK(aclrtDestroyStream(clientStream));
  ACL_CHECK(aclrtDestroyStream(serverStream));
  ACL_CHECK(aclrtResetDevice(rankId));
  ACL_CHECK(aclFinalize());
  ```

## 数据面通信与计算执行

### Client Kernel入口

MC2 Client运行在AIV上。本指南以Vector计算场景为例，Kernel使用`__vector__`入口，通信Client和计算逻辑写在同一个Kernel中。

Kernel入口参数由Host启动Kernel时传入，通常包括通信源GM地址、通信目的GM地址、计算输入/输出GM地址、MC2通信资源上下文和通信规模等。`mc2Context`使用Host侧`Mc2AcquireCcResCtx`返回的通信资源上下文地址，Client通过`InitV2`使用该上下文访问通信资源。

  ```cpp
  extern "C" __vector__ __global__ void mc2_operator_custom(
      GM_ADDR srcGM, GM_ADDR dstGM, GM_ADDR computeInGM, GM_ADDR computeOutGM,
      GM_ADDR mc2Context, uint64_t count, uint32_t dataType)
  {
      // 本示例只下发AIV，非AIV直接返回。实际算子按自身AIC/AIV任务划分处理。
      if (g_coreType != AscendC::AIV) {
          return;
      }

      AscendC::InitSocState();
      AscendC::Hccl<> hccl;
      hccl.InitV2(mc2Context, nullptr);

      // 在通信输入就绪后提交通信任务，并按数据依赖执行计算，见下文。
  }
  ```

### 提交通信任务

Client完成`InitV2`初始化后，调用HCCL通信接口提交通信任务。通信接口由具体通信操作决定，本指南以AllGather为例。

AllGather接口返回`HcclHandle`，该句柄用于后续查询通信完成状态。接口返回成功只表示通信任务已经提交，不表示目的GM中的数据已经可用。

  ```cpp
  constexpr uint32_t repeat = 2;
  const uint64_t roundCount = count / repeat;

  AscendC::HcclHandle handleId = hccl.AllGather<true>(
      srcGM, dstGM, roundCount,
      static_cast<AscendC::HcclDataType>(dataType),
      count, repeat);
  if (handleId < 0) {
      return;
  }
  ```

`AllGather<true>`中的模板参数表示Prepare阶段同步Commit，即源数据准备完成后，Client立即向Server发布该通信任务的执行条件。若使用异步Commit或分批提交，必须保证最后一个通信任务已经Commit，Server才会开始执行数据传输。

`roundCount`表示单轮通信的元素个数，`repeat`表示通信轮数；本示例将`count`个元素分成两轮AllGather传输。执行其他通信操作时，应替换为对应的HCCL通信接口，并按该接口定义设置元素个数、数据类型、归约类型、偏移或重复次数等参数。

### 按数据依赖执行计算

AICore Client Kernel提交通信任务后，可以先执行不读取通信输出的计算。依赖通信结果的计算必须在对应通信任务或通信分片完成后，再读取目的GM。通信完成条件通过通信接口返回的`HcclHandle`和`Wait`确认。

  ```cpp
  // 提交通信任务后，可以先执行不读取通信输出的计算。
  ComputeIndependent(computeInGM, computeOutGM, count);

  // 确认当前通信任务或通信分片已经完成。
  hccl.Wait(handleId);

  // 消费已完成的通信结果，并执行对应Add计算。
  const uint32_t turn = 0;
  AddOneTurn(dstGM, computeInGM, computeOutGM, count, turn);
  ```

`AddOneTurn`表示示例中的Vector Add计算，用户可以根据算子功能替换为其他Ascend C计算逻辑。计算逻辑需要按照通信输出的实际布局读取数据，并将结果写入计算输出缓冲区。

多轮或分块通信时，应为每个分片建立“源数据就绪、通信提交、通信完成、结果消费、缓冲区复用”的数据依赖。某个分片完成后即可消费该分片；未完成分片对应的目的GM不能提前读取或交给计算阶段，否则可能读取到不完整的数据。通信仍在使用的源缓冲区和目的缓冲区也不能提前覆盖或复用，否则会产生通信与计算之间的读写冲突。

当前AllGather+Add样例将两轮通信结果全部确认后再执行Add，属于串行计算流程。两轮次数由通信接口的分轮配置决定，不是核间同步本身的要求。

### 核间同步和结束通信协作

当多个AIV共同参与同一个MC2通信任务时，参与通信的AIV需要按照相同顺序提交通信请求，并对对应通信任务完成相应的`Wait`。多轮通信时，各参与核需要处理匹配的通信轮次，保持对共享通信上下文的访问顺序一致。

每个参与核完成自己负责的通信结果消费和计算后，调用`AscendC::SyncAll<true>`等待其他参与核。该同步用于协调参与核的执行进度，不替代通信接口的`Wait`。只有所有参与核都完成本轮通信确认和结果消费后，才能调用`hccl.Finalize()`结束Client与Server之间的通信协作。

  ```cpp
  // 当前AIV已完成自身通信任务的Wait和结果消费。
  /* ... communication result consumption and computation ... */

  // 等待所有参与AIV完成本轮通信结果消费和计算。
  AscendC::SyncAll<true>();

  // 结束Client与Server之间的通信协作。
  hccl.Finalize();
  ```

如果某个参与核在其他核仍然提交通信任务、等待通信结果或消费通信输出时提前调用`Finalize`或退出，可能导致通信协作提前结束，其他核继续访问通信上下文时无法完成。因此，所有参与核都应在本地通信和计算完成后，再统一进入核间同步和结束流程。

## 注意事项

- MC2属于代理模式，Client和Server必须使用同一个`OpResCtx`，并保持相同的通信命令和数据类型配置。
- AICPU_TS路径下Server会阻塞等待Client请求，Server和Client应使用独立stream，避免同队列顺序依赖。
- `HcclHandle`有效只代表任务提交成功，通信结果必须经过`Wait`确认后才可读取。
- `Wait`前不能消费目的GM；通信完成前不能覆盖源GM或复用目的GM。
- 多AIV场景下，通信调用顺序、完成确认次数、核间同步和结束通信协作的时机必须一致。
- 分块或多轮通信时，按分片建立数据依赖，不能把未完成分片当作完整通信结果处理。
- 通信地址、长度单位、数据类型、对齐和算法配置必须符合当前HCCL接口约束；MC2 Host API和AICore HCCL客户端签名以当前CANN版本为准。
- CCU路径与AICPU_TS路径的通信引擎、算法配置、Server启动接口和客户端模板可能不同；两者的高阶通信API提交、等待和结束语义基本一致，切换路径时应同时核对Host与Kernel两侧配置。
