# 基于MC2的通算算子开发指南

## 概述

MC2（Massive Communication Computing）是代理模式在Ascend C通信编程中的一类具体实现。Host侧初始化ACL运行时，创建HCCL通信域，并申请MC2通信资源上下文；AICore Kernel作为通信Client提交通信任务；AICPU_TS或CCU任务作为通信Server接收Client请求，驱动通信通路完成数据传输，并向Client发布完成状态。

MC2通算算子的核心价值是通信与计算重叠。通信任务提交后，通信数据传输由AICPU_TS或CCU服务端执行，AICore可以继续执行与本轮通信结果没有依赖的计算；待通信结果就绪后，AICore再读取通信输出并执行依赖计算，从而将部分通信时间与计算时间重叠，减少串行等待，提高算子整体性能。

本指南介绍MC2通算算子的通用开发流程。文中以AllGather作为通信接口示例，Host和Kernel代码按AICPU_TS Server路径展开；CCU同为MC2支持的Server路径，其高阶通信API的提交、等待和结束语义与AICPU_TS路径基本一致，差异主要体现在通信引擎、算法配置、Server启动接口和Client类型上。计算部分使用可替换的占位函数表示，实际算子可以替换为其他集合通信或点对点通信接口，并将计算阶段替换为Vector、Matrix或其他Ascend C计算逻辑。

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

MC2通算算子开发分为控制面资源准备和数据面通信执行两阶段。

控制面流程：

1. 初始化ACL运行环境，绑定Device，创建任务流。
2. 创建HCCL通信域。
3. 分配通信输入、通信输出和计算缓冲区。
4. 创建MC2参数对象并配置通信域、算法、数据类型和通信引擎。
5. 申请MC2通信资源上下文。
6. 启动AICPU_TS或CCU通信Server。
7. 启动携带通信上下文的AICore Client Kernel。
8. 同步任务流，回收结果并释放资源。

数据面流程：

1. AICore Client初始化HCCL客户端。
2. 提交通信任务并发布执行条件。
3. 执行与通信结果无依赖的计算。
4. 调用`Wait`等待通信完成。
5. 消费通信结果并执行依赖通信输入的计算。
6. 所有参与核完成后执行核间同步，并通知Server结束。

```text
Host控制面:
  HCCL domain -> MC2 args -> OpResCtx -> Server launch -> Client launch

AICore Client:
  InitV2 -> Prepare/Commit -> independent compute -> Wait -> consume result -> Finalize

AICPU_TS/CCU Server:
  receive request -> drive communication path -> publish completion
```

Client和Server是两个独立的Device侧执行实体。Host负责任务下发和同步，本身不直接执行Device侧通信任务。AICPU_TS路径下，Server和Client通常提交到不同stream；CCU路径下，Server启动方式由MC2 CCU接口管理，具体以当前API契约为准。

## 控制面资源准备

### 初始化ACL运行环境

每个Rank绑定一个Device，并创建Host与Device交互所需的stream。AICPU_TS路径需要为Server和Client准备独立stream，避免Server阻塞等待Client消息时与Client形成同队列依赖。

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

通信域内所有Rank必须使用同一份rootInfo建域。root Rank调用`HcclGetRootInfo`生成rootInfo，并通过外部控制通道、文件或框架rendezvous机制分发给所有Rank；所有Rank拿到同一份rootInfo后，分别调用`HcclCommInitRootInfo`。

  ```cpp
  HcclRootInfo rootInfo;
  if (rankId == 0) {
      HcclGetRootInfo(&rootInfo);
  }
  // 将rootInfo分发到所有Rank后，各Rank使用同一份数据建域。

  HcclComm comm = nullptr;
  HCCL_CHECK(HcclCommInitRootInfo(rankSize, &rootInfo, rankId, &comm));

  char commName[COMM_NAME_MAX_LENGTH] = {0};
  HCCL_CHECK(HcclGetCommName(comm, commName));
  ```

rankId是通信域内的逻辑成员编号，可以与物理Device ID不同。实际部署时应由启动脚本或训练框架维护rank与device的映射关系。

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

CCU Server路径与AICPU_TS路径的Host流程一致，差异主要在于通信引擎和Server启动接口：Host侧将通信引擎设置为CCU调度引擎，并调用`Mc2CcKernelLaunch`。

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

CCU路径下，AICore Client需要使用与CCU Server匹配的HCCL客户端类型，例如当前CCU样例中使用`Hccl<AscendC::HcclServerType::HCCL_SERVER_TYPE_CCU>`。通信任务提交、`Wait`和`Finalize`的使用方式与AICPU_TS示例基本一致；接口模板和`InitV2`重载以当前头文件为准。

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

## Kernel侧通信执行

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

      // 提交通信任务、等待完成、消费结果，见下文。
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

### 等待通信完成并消费结果

`Wait`返回后，本轮通信目的内存才可被计算阶段读取。通信提交后、Wait之前，可以执行与目的内存无关的计算。

  ```cpp
  // 与本轮通信结果无关的计算，可以放在Commit之后、Wait之前。
  ComputeIndependent(computeInGM, computeOutGM, count);

  // 等待第1轮通信完成，然后才能读取dstGM中本轮结果。
  hccl.Wait(handleId);
  ConsumeCommunicatedData(dstGM, computeInGM, computeOutGM, count / repeat);
  ```

`ConsumeCommunicatedData`是计算阶段占位函数。在AllGather+Add样例中，该函数读取AllGather输出并执行Add；在其他算子中，可以替换为任意依赖通信结果的Ascend C计算逻辑。计算实现细节参见Ascend C算子开发资料。

多轮通信时，每个分片都应满足“源数据就绪 -> Commit -> Wait -> 结果消费 -> 缓冲区复用”的顺序。不要在所有轮次尚未Wait完成时，将整个目的缓冲区一次性交给计算阶段。

### 核间同步和结束通信

多个AIV共同执行MC2 Client时，所有参与核必须保持相同的通信提交顺序、`Wait`次数和`Finalize`时机。部分核提前结束可能影响其他核的Wait或Server退出。

  ```cpp
  hccl.Wait(handleId);      // 每轮通信各调用一次。
  AscendC::SyncAll<true>(); // 所有参与AIV完成通信和结果消费。
  hccl.Finalize();          // 通知Server当前Client Kernel通信结束。
  ```

`Finalize`表示当前Kernel不再向Server提交通信任务。调用前必须确保所有AIV均已完成通信和结果消费。

## 注意事项

- MC2属于代理模式，Client和Server必须使用同一个`OpResCtx`，并保持相同的通信命令和数据类型配置。
- AICPU_TS路径下Server会阻塞等待Client请求，Server和Client应使用独立stream，避免同队列顺序依赖。
- `HcclHandle`有效只代表任务提交成功，通信结果必须经过`Wait`确认后才可读取。
- `Wait`前不能消费目的GM；通信完成前不能覆盖源GM或复用目的GM。
- 多AIV场景下，通信调用顺序、Wait次数、核间同步和`Finalize`时机必须一致。
- 分块或多轮通信时，按分片建立数据依赖，不能把未完成分片当作完整通信结果处理。
- 通信地址、长度单位、数据类型、对齐和算法配置必须符合当前HCCL接口约束；MC2 Host API和AICore HCCL客户端签名以当前CANN版本为准。
- CCU路径与AICPU_TS路径的通信引擎、算法配置、Server启动接口和客户端模板可能不同；两者的高阶通信API提交、等待和结束语义基本一致，切换路径时应同时核对Host与Kernel两侧配置。
