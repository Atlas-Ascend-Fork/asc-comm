# 基于MC2的通算算子开发指南

## 概述

MC2（Massive Communication Computing）是代理模式在Ascend C通信编程中的一类具体实现。Host侧初始化ACL运行时，创建HCCL通信域，并申请MC2通信资源上下文；AICore Kernel作为通信Client提交通信任务；AICPU_TS或CCU任务作为通信Server接收Client请求，驱动通信通路完成数据传输，并向Client发布完成状态。

MC2通算算子的核心价值是通信与计算重叠。通信任务提交后，通信数据传输由AICPU_TS或CCU服务端执行，AICore可以继续执行与本轮通信结果没有依赖的计算；待通信结果就绪后，AICore再读取通信输出并执行依赖计算，从而将部分通信时间与计算时间重叠，减少串行等待，提高算子整体性能。

本指南介绍MC2通算算子的通用开发流程。文中以AllGather作为通信接口示例，Host和Kernel代码按AICPU_TS Server路径展开；CCU同为MC2支持的Server路径，可能在通信引擎、算法配置、Server启动接口和Client类型等方面存在差异，具体组合应以当前版本API参考和对应样例为准。计算部分使用可替换的占位函数表示，实际算子可以替换为其他集合通信或点对点通信接口，并将计算阶段替换为Vector、Matrix或其他Ascend C计算逻辑。

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

Client和Server是两个独立的Device侧执行实体。Host负责任务下发和同步，本身不直接执行Device侧通信任务。AICPU_TS路径下，Host通过`Mc2AicpuKernelLaunch`启动Server，并将Server与Client下发到不同stream。CCU路径的Server启动接口、Client类型和上下文传递方式需结合当前版本API参考及对应样例确定，本文不将单个CCU样例的配置推广为通用规则。

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

通信输入和通信输出必须位于通信接口可访问的Device GM中。计算输入和计算输出也是Device GM缓冲区；计算需要读取通信输出时，必须先满足通信完成条件。

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

缓冲区大小、数据类型、对齐和地址范围必须与通信接口约束一致。通信完成前不能覆盖源数据，也不能将目的缓冲区交给计算阶段消费。

### 配置MC2通信资源

MC2通过参数对象描述通信资源配置。`Mc2GetCcArgs`创建Host侧参数对象，`Mc2SetCc*`系列接口设置通信域、算法、数据类型和通信引擎，`Mc2AcquireCcResCtx`向HCCL申请Device侧通信资源上下文。

  ```cpp
  // AICPU_TS引擎编号，具体优先使用当前头文件中的枚举定义。
  constexpr uint8_t MC2_COMM_ENGINE_AICPU_TS = 2;
  constexpr uint8_t ccType = static_cast<uint8_t>(AscendC::HcclCMDType::HCCL_CMD_ALLGATHER);
  char algConfig[] = "AllGather=level0:doublering";

  void* ccArgs = nullptr;
  void* opResCtx = nullptr;
  uint32_t opResCtxSize = 0;

  HCCL_CHECK(Mc2GetCcArgs(&ccArgs));
  HCCL_CHECK(Mc2SetCcGroupName(ccArgs, commName));
  HCCL_CHECK(Mc2SetCcAlgConfig(ccArgs, algConfig));
  HCCL_CHECK(Mc2SetCcSrcDataType(ccArgs, HCCL_DATA_TYPE_FP16));
  HCCL_CHECK(Mc2SetCcDstDataType(ccArgs, HCCL_DATA_TYPE_FP16));
  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, MC2_COMM_ENGINE_AICPU_TS));
  HCCL_CHECK(Mc2AcquireCcResCtx(comm, ccType, ccArgs, &opResCtx, &opResCtxSize));
  HCCL_CHECK(Mc2FreeCcArgs(ccArgs));
  ```

`ccType`必须与AICore侧实际调用的通信命令一致；归约类通信还需要按当前API要求设置归约类型等参数。`algConfig`由通信命令、拓扑和算法策略决定，应使用当前HCCL版本支持的配置字符串。

`opResCtx`是MC2通信资源上下文。该上下文必须同时传给Server启动接口和AICore Client Kernel；`Mc2FreeCcArgs`只释放Host侧参数对象，不释放`opResCtx`。`opResCtx`的生命周期由MC2/HCCL资源管理机制管理，销毁通信域前必须确保Server和Client均已完成。

### 启动Server和Client

AICPU_TS Server通过`Mc2AicpuKernelLaunch`下发。Server会等待AICore Client的通信请求，因此应在Client Kernel前下发，并使用独立Server stream。

  ```cpp
  // blockDim为0时，接口内部按1个block处理。
  HCCL_CHECK(Mc2AicpuKernelLaunch(0, serverStream, opResCtx, opResCtxSize));
  ```

随后下发AICore Client Kernel。Kernel参数包括通信源/目的GM、计算缓冲区、`opResCtx`和通信规模等运行时参数。

  ```cpp
  mc2_operator_custom<<<blockDim, nullptr, clientStream>>>(
      sendBuf, recvBuf, computeInBuf, computeOutBuf, opResCtx,
      sendCount, HCCL_DATA_TYPE_FP16);
  ```

在当前CCU AllGather样例中，Host侧将通信引擎设置为CCU调度引擎，并调用`Mc2CcKernelLaunch`启动Server。其他CCU通信操作或配置应以对应样例和当前版本API参考为准。

  ```cpp
  // CCU调度引擎编号，具体优先使用当前头文件中的枚举定义。
  constexpr uint8_t MC2_COMM_ENGINE_CCU_SCHED = 6;
  char algConfig[] = "CcuSchedAllGatherSoleMesh";

  HCCL_CHECK(Mc2SetCcCommEngine(ccArgs, MC2_COMM_ENGINE_CCU_SCHED));
  HCCL_CHECK(Mc2SetCcAlgConfig(ccArgs, algConfig));
  HCCL_CHECK(Mc2AcquireCcResCtx(comm, ccType, ccArgs, &ccResCtx, &ccResCtxSize));
  HCCL_CHECK(Mc2FreeCcArgs(ccArgs));

  Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize);
  ```

当前CCU AllGather样例中的AICore Client使用`Hccl<AscendC::HcclServerType::HCCL_SERVER_TYPE_CCU>`，并将Host侧获取的同一资源上下文传入`InitV2`。其他CCU配置的Client类型、上下文传递方式以及通信任务接口，应以对应样例和当前头文件为准。

### 同步和销毁通信资源

通信任务完成后，先同步Client stream，确认AICore Kernel已经完成`Wait`、结果消费和`Finalize`；AICPU_TS路径再同步Server stream，等待Server退出。

  ```cpp
  ACL_CHECK(aclrtSynchronizeStream(clientStream));
  ACL_CHECK(aclrtSynchronizeStream(serverStream));

  // 拷回通信结果或计算结果。
  ACL_CHECK(aclrtMemcpy(hostResult, recvSize, computeOutBuf, recvSize, ACL_MEMCPY_DEVICE_TO_HOST));
  ```

资源释放顺序与创建顺序相反：释放Device缓冲区，销毁通信域，销毁stream，重置Device，最后执行ACL去初始化。`opResCtx`不需要用户单独释放时，必须在其所属通信域销毁前确认Server/Client已完成。

  ```cpp
  ACL_CHECK(aclrtFree(sendBuf));
  ACL_CHECK(aclrtFree(recvBuf));
  ACL_CHECK(aclrtFree(computeInBuf));
  ACL_CHECK(aclrtFree(computeOutBuf));

  HCCL_CHECK(HcclCommDestroy(comm));
  ACL_CHECK(aclrtDestroyStream(clientStream));
  ACL_CHECK(aclrtDestroyStream(serverStream));
  ACL_CHECK(aclrtResetDevice(rankId));
  ACL_CHECK(aclFinalize());
  ```

## 数据面通信与计算执行

### Client Kernel入口

MC2通信Client在AIV上执行。Kernel入口接收通信GM、计算GM、MC2上下文和通信规模参数。AIC核不调用HCCL客户端接口，可直接返回。

  ```cpp
  extern "C" __vector__ __global__ void mc2_operator_custom(
      GM_ADDR srcGM, GM_ADDR dstGM, GM_ADDR computeInGM, GM_ADDR computeOutGM,
      GM_ADDR mc2Context, uint64_t count, uint32_t dataType)
  {
      if (g_coreType != AscendC::AIV) {
          return;
      }

      AscendC::InitSocState();
      AscendC::Hccl<> hccl;
      hccl.InitV2(mc2Context, nullptr);

      // 在通信输入就绪后提交通信任务，并按数据依赖执行计算，见下文。
  }
  ```

`mc2Context`必须使用Host侧`Mc2AcquireCcResCtx`返回的同一个上下文地址。不要将Host侧`ccArgs`指针或通信域句柄直接传入Kernel。

### 提交通信任务

通信接口由具体算子决定。以两轮AllGather为例，每次通信搬`count/2`个元素，目的侧同一来源Rank的相邻轮次间隔`count`个元素，共执行2轮。

  ```cpp
  constexpr uint32_t repeat = 2;
  AscendC::HcclHandle handleId = hccl.AllGather<true>(
      srcGM, dstGM, count / repeat,
      static_cast<AscendC::HcclDataType>(dataType),
      count, repeat);
  if (handleId < 0) {
      return;
  }
  ```

模板参数`true`表示当前接口在Prepare阶段同步Commit，即源数据准备完成后立即向Server发布执行条件。若使用异步Commit或分批提交，必须保证最后一个任务已经Commit，Server才会开始执行。

接口返回`HcclHandle`只表示通信请求已经提交，不表示目的GM数据已经可用。其他通信操作应替换为对应的HCCL高阶接口，并按该接口的参数定义设置数据类型、归约类型、偏移或重复次数。

### 按数据依赖执行计算

通信任务提交后，与通信输出无关的计算可以继续执行；依赖通信输出的计算，必须在对应通信结果就绪后读取目的GM。具体完成条件由通信接口的`Wait`确认。

  ```cpp
  // 与通信输出无关的计算，可以放在通信提交之后、完成确认之前。
  ComputeIndependent(computeInGM, computeOutGM, count);

  // 确认通信完成后，才能读取dstGM中的通信输出。
  hccl.Wait(handleId);
  ConsumeCommunicatedData(dstGM, computeInGM, computeOutGM, count / repeat);
  ```

`ConsumeCommunicatedData`是计算阶段占位函数。在AllGather+Add样例中，该函数读取AllGather输出并执行Add；在其他算子中，可以替换为任意依赖通信结果的Ascend C计算逻辑。计算实现细节参见Ascend C算子开发资料。

多轮或分块通信时，按分片建立“源数据就绪 -> 通信提交 -> 通信完成 -> 结果消费 -> 缓冲区复用”的依赖。不要在对应分片尚未完成时，将整个目的缓冲区一次性交给计算阶段。

### 核间同步和结束通信协作

多个AIV共同执行MC2 Client时，所有参与核必须保持相同的通信提交顺序、完成确认次数和结束通信协作的时机。部分核提前结束可能影响其他核的完成确认或Server退出。

  ```cpp
  hccl.Wait(handleId);      // 按通信接口要求确认完成。
  AscendC::SyncAll<true>(); // 所有参与AIV完成通信和计算。
  hccl.Finalize();          // 结束当前Client Kernel与Server的通信协作。
  ```

`Finalize`表示当前Kernel不再向Server提交通信任务。调用前必须确保所有AIV均已完成通信和结果消费。

## 注意事项

- MC2属于代理模式，Client和Server必须使用同一个`OpResCtx`，并保持相同的通信命令和数据类型配置。
- AICPU_TS路径下Server会阻塞等待Client请求，Server和Client应使用独立stream，避免同队列顺序依赖。
- `HcclHandle`有效只代表任务提交成功，通信结果必须经过`Wait`确认后才可读取。
- `Wait`前不能消费目的GM；通信完成前不能覆盖源GM或复用目的GM。
- 多AIV场景下，通信调用顺序、完成确认次数、核间同步和结束通信协作的时机必须一致。
- 分块或多轮通信时，按分片建立数据依赖，不能把未完成分片当作完整通信结果处理。
- 通信地址、长度单位、数据类型、对齐和算法配置必须符合当前HCCL接口约束；MC2 Host API和AICore HCCL客户端签名以当前CANN版本为准。
- CCU路径与AICPU_TS路径的通信引擎、算法配置、Server启动接口和客户端模板可能不同；两者的高阶通信API提交、等待和结束语义基本一致，切换路径时应同时核对Host与Kernel两侧配置。
