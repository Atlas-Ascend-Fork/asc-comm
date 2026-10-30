# 基于Hcomm的通信算子开发指南

## 概述

Hcomm是AI Core侧点对点通信接口，通过`AscendC::Hcomm`模板选择通信协议，提供`WriteNbi`/`ReadNbi`/`WriteValueNbi`/`WriteWithNotifyNbi`/`WriteReduceNbi`（带归约的写任务）/`AtomicFAA`/`AtomicCAS`等单边数据面能力及`Commit`/`Drain`提交与完成语义。Hcomm基于显式的`ChannelHandle`通信通道与注册内存工作：开发者通过HCCL（Huawei Collective Communication Library，华为集合通信库）接口获取对端链路并创建通道，Kernel侧使用对端注册内存的远端地址直接提交读写任务。

本指南以AllGather为例，介绍Hcomm通信算子的完整开发流程。AllGather将每个rank的本端分片聚合到通信域内所有rank：各rank通过`WriteNbi`将本端分片写入其余每个对端缓冲区的对应偏移，通信完成后所有rank的缓冲区均持有按rankId排列的全量数据。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 通信协议 | 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`，各协议支持的具体接口参见[AI Core Hcomm API](../api/aicore/hcomm/README.md) |
| 通信引擎 | `COMM_ENGINE_AIV` |

<!-- npu="950" id1 -->
> [!NOTE]说明
> 仅Ascend 950PR&950DT系列产品支持实现Hcomm算子。单卡环境仅支持编译验证，实际运行需要至少2张NPU。
<!-- end id1 -->

## 总体流程

Hcomm通信算子开发分为控制面资源准备和数据面通信执行两阶段。

控制面流程：

1. 初始化运行环境
2. 创建通信域
3. 分配内存并注册通信内存
4. 获取对端链路并创建通道
5. 获取对端注册内存
6. 下发通道与内存地址到Kernel
7. 销毁通信资源

数据面流程：

1. 预处理
2. 提交通信任务
3. 等待完成

## 控制面资源准备

### 初始化运行环境

  ```cpp
  uint32_t rank;      // 当前rankId
  uint32_t nranks;    // rank总数
  aclInit(nullptr);
  aclrtSetDevice(static_cast<uint32_t>(rank));
  aclrtStream stream = nullptr;
  aclrtCreateStream(&stream);
  ```

### 创建通信域

通信域内所有rank必须共用同一份rootInfo建域（参见[`HcclGetRootInfo`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclGetRootInfo.md)与[`HcclCommInitRootInfo`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCommInitRootInfo.md)）。Hcomm流程需通过`HcclCommConfig`创建通信域，并设置`hcclWorldRankID`为当前全局rank，对应接口参见[`HcclCommConfigInit`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCommConfigInit.md)与[`HcclCommInitRootInfoConfig`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCommInitRootInfoConfig.md)。

样例工程通过以rank 0为中心的TCP socket控制通道交换rootInfo，参见`examples/aicore/utils/rank_sync.h`与`examples/aicore/utils/hccl_comm_init.h`。也可采用文件交换等其他方式：由root节点将rootInfo写入共享路径下的文件，其他rank等待文件就绪后读取同一份rootInfo。

  ```cpp
  // 控制通道endpoint（由命令行传入），形如"tcp://ip:port"
  std::string ctrlEndpoint;
  // 建立TCP控制通道：rank 0监听endpoint，其余rank连接
  examples::RankSyncContext sync = rank == 0U ? examples::RankSyncContext::Listen(ctrlEndpoint, nranks) :
                                                examples::RankSyncContext::Connect(rank, nranks, ctrlEndpoint);

  // 配置通信域创建参数
  HcclCommConfig config;
  HcclCommConfigInit(&config);
  config.hcclWorldRankID = rank;

  // 生成root节点的rank标识信息：由root节点生成，通信域内所有rank必须共用同一份rootInfo
  HcclRootInfo rootInfo = {};
  if (rank == 0) {
      HcclGetRootInfo(&rootInfo);
  }
  // 通过控制通道AllGather交换rootInfo，所有rank取用rank 0的rootInfo
  std::vector<HcclRootInfo> allRootInfo(nranks);
  sync.Allgather(examples::kTagRootInfo, &rootInfo, sizeof(rootInfo), allRootInfo.data());
  // 所有rank使用同一份rootInfo创建通信域
  HcclComm hcclComm;
  HcclCommInitRootInfoConfig(nranks, &allRootInfo[0], rank, &config, &hcclComm);
  ```

### 分配内存并注册通信内存

AllGather场景下，通信缓冲区需容纳`nranks`个分片：本端分片预置于`rankId * SHARD_SIZE`偏移，作为Kernel侧写入各对端的源数据；该缓冲区同时作为AllGather结果缓冲区，接收其余rank写入的分片。

分配通信缓冲区，并通过[`HcclCommMemReg`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclCommMemReg.md)将本端缓冲区注册为携带tag的通信内存，供对端通过通道识别与访问：

  ```cpp
  constexpr uint64_t SHARD_SIZE = 4096U;               // 单个分片大小
  const uint64_t COMM_BUF_SIZE = SHARD_SIZE * nranks;  // 容纳nranks个分片
  constexpr const char* MEMORY_TAG = "shareBuf";

  // 分配本端通信缓冲区
  void* devBuf = nullptr;
  aclrtMalloc(&devBuf, COMM_BUF_SIZE, ACL_MEM_MALLOC_HUGE_FIRST);

  // 初始化本端分片：写入本端rankId对应的分片位置
  std::vector<uint8_t> hostBuffer(static_cast<size_t>(COMM_BUF_SIZE), 0U);
  for (uint64_t i = 0; i < SHARD_SIZE; i++) {
      hostBuffer[rank * SHARD_SIZE + i] = static_cast<uint8_t>(rank);  // 按业务语义填充本端分片
  }
  aclrtMemcpy(devBuf, COMM_BUF_SIZE, hostBuffer.data(), COMM_BUF_SIZE, ACL_MEMCPY_HOST_TO_DEVICE);

  // 注册通信内存：tag用于对端识别，需各rank统一约定
  CommMem registeredMemory = {};
  registeredMemory.type = COMM_MEM_TYPE_DEVICE;
  registeredMemory.addr = devBuf;
  registeredMemory.size = COMM_BUF_SIZE;
  HcclMemHandle memHandle;
  HcclCommMemReg(hcclComm, MEMORY_TAG, &registeredMemory, &memHandle);
  ```

### 获取对端链路并创建通道

通过包含当前rank的拓扑模型获取到对端的通信链路，配置通道描述并创建通道，涉及接口参见[`HcclRankGraphGetLayers`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/topo_info_query/HcclRankGraphGetLayers.md)、[`HcclRankGraphGetLinks`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/topo_info_query/HcclRankGraphGetLinks.md)、[`HcclChannelDescInit`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelDescInit.md)与[`HcclChannelAcquire`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)。通道为点对点资源，每个对端需单独创建通道；AllGather场景下本端与其余`nranks - 1`个对端各建一条通道：

  ```cpp
  // 查询包含当前rank的拓扑层级编号列表以及拓扑层级数量
  uint32_t* networkLayers = nullptr;
  uint32_t networkLayerCount = 0;
  HcclRankGraphGetLayers(hcclComm, &networkLayers, &networkLayerCount);

  std::vector<ChannelHandle> channels;  // 每个对端一条通道
  for (uint32_t offset = 1U; offset < nranks; offset++) {
      uint32_t peerRank = (rank + offset) % nranks;

      // 获取到对端的链路
      CommLink* links = nullptr;
      uint32_t linkCount = 0;
      HcclRankGraphGetLinks(hcclComm, networkLayers[0], rank, peerRank, &links, &linkCount);

      // 初始化通道描述：远端rank、通道协议、两端endpoint与本端注册内存
      HcclChannelDesc channelDesc;
      HcclChannelDescInit(&channelDesc, 1U);
      channelDesc.remoteRank = peerRank;
      // RoCE（RDMA over Converged Ethernet，基于以太网的RDMA协议）适配：将本处与下方链路匹配的协议常量同步改为COMM_PROTOCOL_ROCE（需组网提供RoCE链路）
      channelDesc.channelProtocol = COMM_PROTOCOL_UB_CTP;
      channelDesc.memHandles = &memHandle;
      channelDesc.memHandleNum = 1U;
      for (uint32_t i = 0; i < linkCount; i++) {
          if (links[i].linkAttr.linkProtocol == COMM_PROTOCOL_UB_CTP) {
              channelDesc.localEndpoint = links[i].srcEndpointDesc;
              channelDesc.remoteEndpoint = links[i].dstEndpointDesc;
              break;
          }
      }

      // 获取通道句柄
      ChannelHandle channel = 0;
      HcclChannelAcquire(hcclComm, COMM_ENGINE_AIV, &channelDesc, 1U, &channel);
      channels.emplace_back(channel);
  }
  ```

### 获取对端注册内存

通过通道查询对端注册的通信内存，按tag匹配得到远端缓冲区地址（为聚焦调用流程，Host侧示例省略返回值检查与错误处理），参见[`HcclChannelGetRemoteMems`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetRemoteMems.md)。Kernel侧基于该地址加偏移计算远端写入位置：

  ```cpp
  std::vector<uint64_t> remoteBufs(channels.size(), 0U);
  for (uint32_t i = 0; i < channels.size(); i++) {
      uint32_t remoteMemoryCount = 0;
      CommMem* remoteMemories = nullptr;
      char** memoryTags = nullptr;
      HcclChannelGetRemoteMems(hcclComm, channels[i], &remoteMemoryCount, &remoteMemories, &memoryTags);

      for (uint32_t j = 0; j < remoteMemoryCount; j++) {
          if (memoryTags[j] != nullptr && strcmp(memoryTags[j], MEMORY_TAG) == 0) {
              remoteBufs[i] = reinterpret_cast<uint64_t>(remoteMemories[j].addr);
              break;
          }
      }
  }
  ```

### 下发句柄到Kernel

将各对端`channel`、本端缓冲区地址`devBuf`与对端缓冲区地址`remoteBufs`组装为Host与Kernel共享的上下文（结构定义参见[Kernel侧通信执行](#kernel侧通信执行)，需在Host与Kernel共享的头文件中定义）后下发：

  ```cpp
  void* devContext = nullptr;
  aclrtMalloc(&devContext, sizeof(AllGatherContext), ACL_MEM_MALLOC_HUGE_FIRST);

  AllGatherContext context = {};
  context.localBufferAddr = reinterpret_cast<uint64_t>(devBuf);
  context.rankId = rank;
  context.nranks = nranks;
  context.peerNum = static_cast<uint32_t>(channels.size());
  for (uint32_t i = 0; i < channels.size(); i++) {
      context.channelHandles[i] = channels[i];
      context.remoteBufferAddrs[i] = remoteBufs[i];
  }
  aclrtMemcpy(devContext, sizeof(context), &context, sizeof(context), ACL_MEMCPY_HOST_TO_DEVICE);

  all_gather_kernel<<<1U, nullptr, stream>>>(devContext);
  ```

### 销毁通信资源

通信任务完成后，销毁通信资源（参见[`HcclCommDestroy`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCommDestroy.md)）。样例工程中通道与注册内存随通信域统一释放，参见样例[hcomm_urma_write_read_nbi](../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md)：

  ```cpp
  // 销毁通信域（通道与注册内存随通信域统一释放）
  HcclCommDestroy(hcclComm);
  // 释放申请的内存
  aclrtFree(devContext);
  aclrtFree(devBuf);
  // 去初始化
  aclrtDestroyStream(stream);
  aclrtResetDevice(static_cast<uint32_t>(rank));
  aclFinalize();
  ```

## Kernel侧通信执行

Kernel入口函数如下，通道句柄与本端、对端缓冲区地址经`AllGatherContext`上下文以入参形式传入，后续步骤在该函数内展开。Host侧与Kernel侧的协议常量必须一致（如均为`COMM_PROTOCOL_UB_CTP`）：

  ```cpp
  #include "hcomm/hcomm.h"

  constexpr uint32_t MAX_PEERS = 16U;  // 对端数上限，按组网规模调整
  constexpr uint64_t SHARD_SIZE = 4096U;

  struct AllGatherContext {
      uint64_t localBufferAddr;
      uint64_t channelHandles[MAX_PEERS];
      uint64_t remoteBufferAddrs[MAX_PEERS];
      uint32_t rankId;
      uint32_t nranks;
      uint32_t peerNum;  // 对端数量，即nranks - 1
  };

  extern "C" __vector__ __global__ __aicore__ void all_gather_kernel(GM_ADDR context)
  {
      // 后续Kernel侧通信执行任务在该函数内展开
  }
  ```

Kernel侧通信接口分为普通接口和批量接口两类，均通过`AscendC::Hcomm`对象调用：

- 普通接口：直接以`ChannelHandle`为入参，每次调用提交任务，需先调用`Init`初始化UB（Unified Buffer，统一缓冲区）临时工作区。
- 批量接口：以批量句柄为入参，多个任务组装后统一提交，减少提交开销，UB工作区由`MakeBatchHandle`绑定。

### 预处理

申请UB临时工作区（不小于512字节），创建`AscendC::Hcomm`对象并调用`Init`初始化工作区，随后解析上下文中的通道与地址信息：

  ```cpp
  // 初始化UB临时工作区大小：不得小于512字节
  constexpr uint32_t HCOMM_WORKSPACE_SIZE = 512U;

  // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
  alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_WORKSPACE_SIZE];

  // 创建Hcomm对象并初始化普通接口使用的临时工作区
  // RoCE适配：模板协议改为AscendC::COMM_PROTOCOL_ROCE，与Host侧通道协议一致；
  AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
  int32_t ret = hcomm.Init(hcommLocal, HCOMM_WORKSPACE_SIZE);

  // 解析上下文：本端缓冲区地址与rank信息
  __gm__ AllGatherContext* ctx = reinterpret_cast<__gm__ AllGatherContext*>(context);
  __gm__ uint8_t* localBuf = reinterpret_cast<__gm__ uint8_t*>(ctx->localBufferAddr);
  uint32_t rankId = ctx->rankId;
  ```

### 提交通信任务

AllGather：遍历全部对端通道，将本端分片（位于`rankId * SHARD_SIZE`偏移）写入每个对端缓冲区的相同偏移。通信完成后，每个rank的缓冲区在`k * SHARD_SIZE`偏移处持有rank k的分片，即为AllGather结果：

  ```cpp
  for (uint32_t i = 0U; i < ctx->peerNum; i++) {
      AscendC::ChannelHandle channel = ctx->channelHandles[i];
      __gm__ uint8_t* remoteBuf = reinterpret_cast<__gm__ uint8_t*>(ctx->remoteBufferAddrs[i]);
      __gm__ uint8_t* remoteDst = remoteBuf + static_cast<uint64_t>(rankId) * SHARD_SIZE;
      __gm__ uint8_t* localSrc = localBuf + static_cast<uint64_t>(rankId) * SHARD_SIZE;
      ret = hcomm.WriteNbi(channel, remoteDst, localSrc, SHARD_SIZE);
  }
  ```

通信任务数量较多时，可设置`commit = false`延迟提交以减少提交开销，参见[延迟提交与批量优化](#延迟提交与批量优化)。

### 等待完成

逐通道调用`Drain`阻塞等待该通道全部任务完成：

  ```cpp
  for (uint32_t i = 0U; i < ctx->peerNum; i++) {
      ret = hcomm.Drain(ctx->channelHandles[i]);
  }
  ```

`Drain`仅保证本端已提交通道的任务完成。AllGather要求数据交换全部结束后结果才完整：Host侧在`aclrtSynchronizeStream`后，需通过rank间同步（如TCP控制通道Barrier）确认所有rank的Kernel均已完成，再校验或使用AllGather结果；若需要设备侧感知数据到达，可使用`WriteWithNotifyNbi`与`AtomicFAA`，参见[通知与原子操作](#通知与原子操作)。

## 通知与原子操作

`WriteWithNotifyNbi`在写数据后向对端notify地址写入通知值，可用于对端感知数据到达；`AtomicFAA`/`AtomicCAS`对远端注册内存执行原子累加与比较交换。上述接口仅支持`COMM_PROTOCOL_UB_CTP`协议：

  ```cpp
  // channel为本端通道句柄，remoteDst/localSrc为远端目的与本端源GM地址，len为写入长度
  // notifyAddr、remoteCounter、remoteValue为对端注册内存中的地址，fetchAddr为本端取回旧值的地址
  // notifyVal/addVal/compareVal/swapVal为业务约定的通知值与原子操作数
  // 写数据并向对端notifyAddr写入notifyVal
  ret = hcomm.WriteWithNotifyNbi(channel, remoteDst, localSrc, len, notifyAddr, notifyVal);
  // 远端原子累加
  ret = hcomm.AtomicFAA<uint64_t>(channel, remoteCounter, fetchAddr, addVal);
  // 远端原子比较交换
  ret = hcomm.AtomicCAS<uint64_t>(channel, remoteValue, fetchAddr, compareVal, swapVal);
  ```

上述接口可对应实现[执行模型](../programming_model/execution_model.md)同步机制章节中的本端同步与远端同步：

### 本端同步

`Drain`即本端同步：阻塞等待指定通道上已提交的全部通信任务完成（内部轮询完成队列CQ）。以远端读为例，提交读任务后必须先完成本端同步再访问读取结果，否则可能读到未更新完毕的数据：

  ```cpp
  // 前置条件：channel为本端通道句柄，remoteSrc为对端注册内存中的源GM地址，localDst为本端目的GM地址，len为读取长度
  ret = hcomm.ReadNbi(channel, localDst, remoteSrc, len);  // 单边读：将对端src数据读入本端dst
  ret = hcomm.Drain(channel);                              // 本端同步：等待该通道已提交任务完成
  // 本端同步完成，localDst中的数据可安全访问
  ```

### 远端同步

`Drain`仅确认本端任务完成，无法感知对端发送给本端的数据是否已就绪。需要设备侧感知数据到达时，采用点对点远端同步：对端在写数据后发送同步信号，本端轮询该信号，确认后再访问数据。

写侧在写数据的同时携带同步信号：`WriteWithNotifyNbi`一次调用完成写数据与写通知，`notifyVal`按同步轮次递增；计数型信号可改为`WriteNbi`写数据后追加`AtomicFAA`对对端计数器原子累加（调用形式参见上文）。任务提交后调用`Drain`等待完成：

  ```cpp
  // notifyAddr为对端（读侧）注册内存中的notify地址
  ret = hcomm.WriteWithNotifyNbi(channel, remoteDst, localSrc, len, notifyAddr, notifyVal);
  ret = hcomm.Drain(channel);
  ```

读侧轮询本端notify地址直至达到预期值，确认对端数据已完整写入：

  ```cpp
  // notifyAddr为本端注册内存中的notify地址（由写侧写入），需32字节对齐
  // expectedNotifyVal为本端等待的预期值，按同步轮次递增
  AscendC::GlobalTensor<uint64_t> notifyGm;
  notifyGm.SetGlobalBuffer(reinterpret_cast<__gm__ uint64_t*>(notifyAddr));

  // DataCopy搬运量按32字节对齐，一次读取4个uint64_t
  constexpr uint32_t SYNC_COUNT = 4U;
  // 准备UB工作区；alignas(32)保证起始地址按32字节对齐
  alignas(32) __ubuf__ uint64_t syncWorkspace[SYNC_COUNT];
  // 由UB工作区起始地址构造同步读取LocalTensor
  AscendC::LocalTensor<uint64_t> syncLocal(
      AscendC::TPosition::VECIN,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(syncWorkspace)), SYNC_COUNT);
  uint64_t observed = 0U;
  do {
      AscendC::DataCopy(syncLocal, notifyGm, SYNC_COUNT);
      // MTE2 -> V：等待搬运完成后，再从UB读取notify值（参照样例03的SetFlag/WaitFlag同步模式，按读取流水线选择事件类型）
      AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);
      AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);
      observed = syncLocal.GetValue(0);
      // V -> MTE2：上一轮读取完成后，才允许下一轮DataCopy覆盖同一UB工作区（避免WAR冒险，参照样例03的反向同步模式）
      AscendC::SetFlag<AscendC::HardEvent::V_MTE2>(EVENT_ID0);
      AscendC::WaitFlag<AscendC::HardEvent::V_MTE2>(EVENT_ID0);
  } while (observed < expectedNotifyVal);
  // 点对点同步完成，可安全访问对端写入的数据
  ```

屏障同步为集合级远端同步，Hcomm未提供对应接口：AllGather等集合语义的前后屏障由Host侧rank间同步完成（参见[等待完成](#等待完成)）；设备侧确需屏障时，可基于`AtomicFAA`对约定rank注册内存中的共享计数器累加，再按点对点同步轮询计数值直至全部rank抵达。屏障同步引入全局等待，应避免滥用；需要远端同步时优先采用点对点同步。

## 延迟提交与批量优化

### 延迟提交

普通接口默认每次调用立即提交；提交任务时设置`commit = false`可延迟到显式调用`Commit`统一提交，减少提交开销：

  ```cpp
  // 前置条件：channel为本端通道句柄，dst为远端注册内存中的目的GM地址，src为本端源GM地址，len为写入长度
  // 延迟提交：只添加任务，不提交
  ret = hcomm.WriteNbi<false>(channel, dst, src, len);
  // 显式统一提交
  ret = hcomm.Commit(channel);
  // 等待该通道全部任务完成
  ret = hcomm.Drain(channel);
  ```

连续积攒的任务槽位不得超过通道的任务提交容量（发送队列深度`sqDepth`），需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。

### 批量提交

需要组装多个任务统一提交时，可使用批量接口。批量接口通过批量句柄组装多个任务并统一提交，不需要调用`Init`，其UB工作区由`MakeBatchHandle`绑定。按通道数量分为单通道批量与多通道批量两种形态。`COMM_PROTOCOL_UB_CTP`协议下同一批次可混合添加读、写和写通知任务；`COMM_PROTOCOL_ROCE`协议下仅支持批量读和写任务。

#### 单通道批量

需要向同一通道提交多个任务时，通过批量句柄组装多个任务并统一提交：

  ```cpp
  // 前置条件：channel为本端通道句柄，remoteAddr用于选择远端注册内存，
  // remoteWriteDst/localWriteSrc/writeLen为写任务的目的/源GM地址与长度，
  // localReadDst/remoteReadSrc/readLen为读任务的目的/源GM地址与长度，均由调用方按业务准备
  // 批量工作区长度：至少64字节，按64字节任务槽划分
  constexpr uint32_t batchBufferLen = 128U;
  // 声明批量UB工作区；alignas(32)保证起始地址按32字节对齐
  alignas(32) __ubuf__ uint8_t batchWorkspace[batchBufferLen];
  // 由UB工作区起始地址构造批量工作区LocalTensor
  AscendC::LocalTensor<uint8_t> batchBuffer(
      AscendC::TPosition::VECOUT,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(batchWorkspace)), batchBufferLen);
  // 创建批量句柄并绑定批量工作区，remoteAddr用于选择远端注册内存
  auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen, remoteAddr);

  // 添加读、写、写通知任务，同一批次可混合使用
  ret = hcomm.WriteNbi(batchHandle, remoteWriteDst, localWriteSrc, writeLen);
  ret = hcomm.ReadNbi(batchHandle, localReadDst, remoteReadSrc, readLen);
  // 统一提交当前批次
  ret = hcomm.BatchCommit(batchHandle);
  // 等待该批量句柄提交的任务完成
  ret = hcomm.Drain(batchHandle);
  ```

`remoteAddr`用于指定本批任务访问的远端注册内存：`COMM_PROTOCOL_UB_CTP`协议下该参数不生效，固定使用第一个远端注册buffer；`COMM_PROTOCOL_ROCE`协议必须显式传入（多SGE场景见[多SGE批量读写](#多sge批量读写)）。`BatchCommit`提交成功后，可以复用批量句柄和工作区继续添加并提交下一批任务。

完整流程参见样例[hcomm_roce_batch_write_read](../../../examples/aicore/hcomm/08_hcomm_roce_batch_write_read/README.md)（COMM_PROTOCOL_ROCE协议）。

#### 多通道批量

需要向多个对端批量提交任务时，Host侧通过`MakeMultiChannelHandle`创建多通道句柄。使用HCCL通信域管理资源时，采用通信域重载，创建的资源随通信域销毁自动释放：

  ```cpp
  #include "hcomm/hcomm_host.h"

  // channelDescs/channelNum：按本地endpoint对通道描述分组后的数组及其数量，由调用方准备
  // sharedQueueTag：多通道共享提交队列的tag，由调用方构造
  MultiChannelHandle multiChannel = 0U;
  HcclResult ret = MakeMultiChannelHandle(
      hcclComm, sharedQueueTag, channelDescs, channelNum, &multiChannel);
  ```

不使用HCCL通信域时，传入Hcomm Endpoint和`HcommChannelDesc`调用另一个重载。Kernel执行并同步完成后，需要先销毁多通道句柄，再销毁Endpoint：

  ```cpp
  // endpointHandle：Hcomm Endpoint句柄，由Endpoint创建接口获得
  HcommResult ret = MakeMultiChannelHandle(
      endpointHandle, channelDescs, channelNum, &multiChannel);

  // Kernel执行并同步完成后，再销毁句柄和Endpoint
  ret = DestroyMultiChannelHandle(multiChannel);
  // HcommEndpointDestroy：销毁Hcomm Endpoint的外部接口，由hcomm外部组件提供（与MakeMultiChannelHandle的Endpoint重载配套）
  ret = HcommEndpointDestroy(endpointHandle);
  ```

AI Core侧通过`GetHandleRef`选择逻辑通道（对应`channelDescs`数组下标）和远端注册内存，再通过多通道批量句柄添加、提交并等待任务：

  ```cpp
  // remoteBuffers：各逻辑通道对应的对端注册内存地址；localBuffer/dataLen：本端数据地址与长度
  auto multiBatchHandle = hcomm.MakeBatchHandle(
      static_cast<AscendC::MultiChannelHandle>(multiChannel), batchBuffer, batchBufferLen);

  for (uint32_t channelIndex = 0U; channelIndex < channelNum; ++channelIndex) {
      auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, remoteBuffers[channelIndex]);
      ret = hcomm.WriteNbi(peerBatchHandle, remoteBuffers[channelIndex], localBuffer, dataLen);
  }
  ret = hcomm.BatchCommit(multiBatchHandle);
  ret = hcomm.Drain(multiBatchHandle);
  ```

切换逻辑通道或远端注册内存时，需要重新调用`GetHandleRef`；对同一个多通道批量句柄重复调用时，返回的引用指向同一个内层BatchHandle，并随最近一次调用更新其逻辑通道和远端注册内存选择。

完整流程参见样例[hcomm_urma_batch_write](../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md)。

## 多SGE批量读写

支持多SGE（Scatter Gather Element）批量读写：单次`WriteNbi`/`ReadNbi`通过`BufDesc`数组描述多个本端离散数据段，对远端一段连续内存聚合读写，适合源数据分块离散存放的场景。

多SGE由`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议支持，且仅通过批量接口实现：普通接口`WriteNbi`/`ReadNbi`不支持`BufDesc`入参，多SGE任务需经`MakeBatchHandle`创建的批量句柄提交。`srcDescs`/`dstDescs`数据段数量在`COMM_PROTOCOL_UB_CTP`协议下为1-13、`COMM_PROTOCOL_ROCE`协议下为1-24。`COMM_PROTOCOL_ROCE`协议下创建批量句柄时需同时绑定远端与本端注册内存：`localAddr`须为本端注册内存中的地址，用于绑定本端内存保护信息；`srcDescs`/`dstDescs`中各数据段的地址须位于绑定注册内存范围内、各数据段`len`及累加总长度均须小于2GB（2 * 1024 * 1024 * 1024字节）。`COMM_PROTOCOL_UB_CTP`协议下`localAddr`不生效，`remoteAddr`固定使用第一个远端注册buffer（参见[单通道批量](#单通道批量)）。各数据段地址与长度的合法性由调用方保证，句柄创建时不校验各数据段地址，违规将在任务提交或执行阶段暴露。

  ```cpp
  // 创建批量句柄：同时绑定远端注册内存与本端注册内存
  auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen, remoteAddr, localAddr);

  // 单次写：将本端SGE_NUM个离散数据段写入远端连续区间remoteDst（SGE_NUM/SEGMENT_STRIDE/SEGMENT_SIZE为业务约定的段数量、步长与段长，remoteDst需位于remoteAddr所在注册内存内）
  AscendC::BufDesc srcDescs[SGE_NUM];
  for (uint32_t i = 0; i < SGE_NUM; i++) {
      srcDescs[i].addr = localAddr + i * SEGMENT_STRIDE;  // 本端离散段地址
      srcDescs[i].len = SEGMENT_SIZE;
  }
  ret = hcomm.WriteNbi(batchHandle, remoteDst, srcDescs, SGE_NUM);

  // 单次读：从远端连续区间remoteSrc读回本端SGE_NUM个离散数据段（remoteSrc需位于remoteAddr所在注册内存内）
  AscendC::BufDesc dstDescs[SGE_NUM];
  // 按相同方式填充dstDescs
  ret = hcomm.ReadNbi(batchHandle, dstDescs, SGE_NUM, remoteSrc);

  // 提交并等待本批任务完成
  ret = hcomm.BatchCommit(batchHandle);
  ret = hcomm.Drain(batchHandle);
  ```

完整流程参见样例[hcomm_roce_multi_sge](../../../examples/aicore/hcomm/09_hcomm_roce_multi_sge/README.md)。

## 多AI Core共享通道

多个AIV（AI Vector，向量核）共享同一个`COMM_PROTOCOL_UB_CTP`通道时，需通过[Lock](../api/aicore/hcomm/Lock.md)和[Unlock](../api/aicore/hcomm/Unlock.md)保护会更新通道状态的访问。每个AI Core可使用独立的Hcomm对象和UB临时工作区，但传入相同的`ChannelHandle`。锁不可重入，每次成功`Lock`后必须由同一AI Core调用`Unlock`，错误退出路径也需要释放锁；接口仅同步同一设备上共享同一通道的AI Core：

  ```cpp
  int32_t lockRet = hcomm.Lock(channel);
  if (lockRet == 0) {
      int32_t opRet = hcomm.WriteNbi(channel, dst, src, len);
      // 即使通信接口返回失败，也必须释放已经成功获取的锁
      int32_t unlockRet = hcomm.Unlock(channel);
      // 分别处理opRet和unlockRet
  }
  ```

## 注意事项

- 普通接口需先调用`Init`提供UB临时工作区，最小512字节；使用`__ubuf__ uint8_t*`时起始地址按32字节对齐，使用`LocalTensor`时需保证tensor容量满足工作区要求。
- Host侧通道协议（`HcclChannelDesc.channelProtocol`）与Kernel侧`Hcomm`模板协议必须一致。
- 通道为点对点资源，每个对端需单独`HcclChannelAcquire`；环形多对端场景建议按rank奇偶错开建链顺序，避免循环等待；`nranks == 2`时前后对端相同，可复用同一条通道。
- 单次`WriteNbi`/`ReadNbi`的数据长度：`COMM_PROTOCOL_UB_CTP`协议下必须不超过256MB，`COMM_PROTOCOL_ROCE`协议下必须小于2GB（2 * 1024 * 1024 * 1024字节）。
- `WriteWithNotifyNbi`、`WriteValueNbi`、`WriteReduceNbi`、`AtomicFAA`和`AtomicCAS`仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 原子操作的数据类型仅支持`int32_t`、`uint32_t`、`int64_t`和`uint64_t`。
