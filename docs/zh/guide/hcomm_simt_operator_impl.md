# 基于Hcomm的SIMT通信算子开发指南

## 概述

Hcomm的AI Core侧SIMT点对点通信接口，通过`AscendC::simt::Hcomm`模板选择通信协议，提供`WriteNbi`/`ReadNbi`/`WriteValueNbi`/`WriteWithNotifyNbi`/`AtomicFAA`/`AtomicCAS`等单边数据面能力及`Drain`完成等待语义。SIMT（Single Instruction Multiple Threads，单指令多线程）路径与SIMD（Single Instruction Multiple Data，单指令多数据）路径（`AscendC::Hcomm`，参见[基于Hcomm的通信算子开发指南](hcomm_operator_impl.md)）功能近似，Host侧控制面流程完全一致，差异集中在Kernel侧：接口运行于SIMT执行空间，无法使用`LocalTensor`与pipe（流水线）同步原语，缓冲区地址按通道缓冲区表下标解析，普通接口由单个lane（SIMT执行模型中的线程通道，同一Kernel内多个lane并行执行）驱动通道（批量接口支持Group（协作组，由`simt_api/cooperative_groups.h`提供）多lane协同提交）。

本指南以AllGather为例，介绍基于Hcomm的SIMT通信算子的完整开发流程：控制面（Host侧）资源准备与SIMD路径一致，直接参考[基于Hcomm的通信算子开发指南](hcomm_operator_impl.md)，仅展开存在差异的步骤；重点描述Kernel侧（数据面）通信接口与SIMD路径的使用差别。AllGather将每个rank的本端分片聚合到通信域内所有rank：各rank通过`WriteNbi`将本端分片写入其余每个对端缓冲区的对应偏移，通信完成后所有rank的缓冲区均持有按rankId排列的全量数据。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 通信协议 | `COMM_PROTOCOL_UB_CTP` |
| 通信引擎 | `COMM_ENGINE_AIV` |

<!-- npu="950" id1 -->
> [!NOTE]说明
> 仅Ascend 950PR&950DT系列产品支持基于Hcomm实现SIMT算子。单卡环境仅支持编译验证，实际运行需要至少2张NPU。
<!-- end id1 -->

## 总体流程

Hcomm SIMT通信算子开发分为控制面资源准备和数据面通信执行两阶段。

控制面流程与SIMD路径完全一致，各步骤的接口说明与代码示例参见[基于Hcomm的通信算子开发指南](hcomm_operator_impl.md#控制面资源准备)，仅步骤5、6由传递64位地址改为传递缓冲区表下标：

1. 初始化运行环境
2. 创建通信域
3. 分配内存并注册通信内存
4. 获取对端链路并创建通道
5. 获取对端注册内存表下标
6. 下发通道与内存下标到Kernel
7. 销毁通信资源

数据面流程：

1. 预处理
2. 提交通信任务
3. 等待完成

## 控制面资源准备

除下述两个差异步骤外，SIMT路径的控制面流程与SIMD路径完全一致：初始化运行环境、创建通信域、分配内存并注册通信内存、获取对端链路并创建通道、销毁通信资源等步骤参见[基于Hcomm的通信算子开发指南](hcomm_operator_impl.md#控制面资源准备)，SIMT完整流程样例参见[simt_hcomm_urma](../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md)。

差异集中在地址传递方式：SIMT路径Kernel侧按通道缓冲区表下标解析缓冲区地址，因此下述两步由传递64位地址改为传递缓冲区表下标。

### 获取对端注册内存表下标

通过通道查询对端注册的通信内存，按tag匹配得到对端内存在远端内存表中的下标（为聚焦调用流程，Host侧示例省略返回值检查与错误处理），参见[`HcclChannelGetRemoteMems`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetRemoteMems.md)。Kernel侧通过该下标经`AscendC::simt::RemoteBufferAddr`解析远端基址：

  ```cpp
  // 记录对端注册内存在远端内存表中的下标，供Kernel侧解析基址
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

### 下发句柄到Kernel

将各对端`channel`与对端缓冲区表下标`remoteBufIdxs`组装为Host与Kernel共享的上下文（结构定义参见[Kernel侧通信执行](#kernel侧通信执行)，需在Host与Kernel共享的头文件中定义）后下发。本端缓冲区无需下发地址：Kernel侧按本地缓冲区表下标解析，下标`0`为通信域的HcclBuffer（由[`HcclChannelAcquire`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)建通道时注册的内部缓冲区），显式注册的内存从下标`1`开始（与`HcclCommMemReg`的注册顺序一致）。

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

## Kernel侧通信执行

SIMT路径与SIMD路径的数据面接口能力基本对齐，Kernel侧使用差别总览如下（各差异点的具体用法参见后续小节与[注意事项](#注意事项)）：

| 差异点 | SIMD路径（`AscendC::Hcomm`） | SIMT路径（`AscendC::simt::Hcomm`） |
| --- | --- | --- |
| 头文件与模板协议 | 头文件`hcomm/hcomm.h`，模板协议支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE` | 头文件`hcomm/hcomm_simt.h`，模板协议仅支持`COMM_PROTOCOL_UB_CTP` |
| Kernel入口形态 | `__vector__ __global__ __aicore__`入口函数内直接展开 | `__global__ __vector__`入口经`asc_vf_call`调用`__simt_vf__`（VF，Vector Function，矢量函数） |
| 执行空间 | 可使用`LocalTensor`与pipe同步原语 | 无法使用`LocalTensor`与pipe同步原语 |
| UB临时工作区 | 普通接口需`Init`绑定不小于512字节的UB工作区 | 无需UB工作区，`Init`固定传`nullptr`和`0` |
| 缓冲区地址 | Host下发本端与对端缓冲区的64位地址，Kernel直接使用 | Host下发缓冲区表下标，Kernel经`LocalBufferAddr`/`RemoteBufferAddr`解析基址 |
| 调用模型 | 由AI Core直接调用 | 普通接口由单个lane驱动通道，其余lane直接返回；Group批量接口由组内多lane协同驱动 |
| 延迟提交 | 显式调用`Commit`统一提交 | 无`Commit`接口，`commit = false`的任务由后续`commit = true`的任务提交时带出，同批任务须由同一lane提交 |
| 批量提交 | 单通道批量与多通道批量（`MultiChannelHandle`），批量工作区至少64字节 | 单lane批量与多lane协同批量（group），批量工作区至少256字节 |
| 通道共享 | 多AI Core共享同一通道需`Lock`/`Unlock`保护 | 不能与SIMD路径的`AscendC::Hcomm`同时驱动同一通道 |

Kernel入口函数采用SIMT形态：`__global__ __vector__`入口通过`asc_vf_call`调用`__simt_vf__`函数。通道句柄与对端缓冲区表下标经`AllGatherContext`上下文以入参形式传入，后续步骤在该函数内展开。Host侧与Kernel侧的协议常量必须一致（SIMT路径为`COMM_PROTOCOL_UB_CTP`）：

  ```cpp
  #include "hcomm/hcomm_simt.h"
  #include "simt_api/asc_simt.h"
  #include "simt_api/cooperative_groups.h"

  constexpr uint32_t MAX_PEERS = 16U;   // 对端数上限，按组网规模调整
  constexpr uint64_t SHARD_SIZE = 4096U;
  // 本端缓冲区表下标：下标0为通信域的HcclBuffer，显式注册内存从1开始
  constexpr uint32_t LOCAL_BUF_IDX = 1U;

  struct AllGatherContext {
      uint64_t channelHandles[MAX_PEERS];
      uint32_t remoteBufIdxs[MAX_PEERS];
      uint32_t rankId;
      uint32_t peerNum;  // 对端数量，即nranks - 1
  };

  __simt_vf__ inline void AllGatherVf(__gm__ AllGatherContext* ctx)
  {
      // 后续Kernel侧通信执行任务在该函数内展开
  }

  extern "C" __global__ __vector__ void simt_all_gather_kernel(__gm__ void* context)
  {
      asc_vf_call<AllGatherVf>(dim3(1), reinterpret_cast<__gm__ AllGatherContext*>(context));
  }
  ```

Kernel侧通信接口分为普通接口和批量接口两类，均通过`AscendC::simt::Hcomm`对象调用：

- 普通接口：直接以`ChannelHandle`为入参，每次调用提交任务，由单个lane调用。
- 批量接口：以批量句柄为入参，多个任务组装后统一提交，UB工作区由`MakeBatchHandle`绑定。

### 预处理

SIMT路径无需申请UB临时工作区：创建`AscendC::simt::Hcomm`对象并调用`Init`即可，两个入参未使用，固定传`nullptr`和`0`。通信任务由单个lane发起，其余lane直接返回：

  ```cpp
  // 单lane驱动通道：仅lane 0发起通信，其余lane直接返回
  if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
      return;
  }

  // 创建Hcomm对象并初始化：无需UB临时工作区
  AscendC::simt::Hcomm<> hcomm;
  int32_t ret = hcomm.Init(nullptr, 0);
  ```

### 提交通信任务

AllGather：遍历全部对端通道，将本端分片（位于`rankId * SHARD_SIZE`偏移）写入每个对端缓冲区的相同偏移。通信完成后，每个rank的缓冲区在`k * SHARD_SIZE`偏移处持有rank k的分片，即为AllGather结果：

  ```cpp
  for (uint32_t i = 0U; i < ctx->peerNum; i++) {
      uint64_t channel = ctx->channelHandles[i];
      // 按通道缓冲区表下标解析本端、对端注册内存基址
      __gm__ uint8_t* localBuf = AscendC::simt::LocalBufferAddr(channel, LOCAL_BUF_IDX);
      __gm__ uint8_t* remoteBuf = AscendC::simt::RemoteBufferAddr(channel, ctx->remoteBufIdxs[i]);
      __gm__ uint8_t* remoteDst = remoteBuf + static_cast<uint64_t>(ctx->rankId) * SHARD_SIZE;
      __gm__ uint8_t* localSrc = localBuf + static_cast<uint64_t>(ctx->rankId) * SHARD_SIZE;
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

`Drain`仅保证本端已提交通道的任务完成。AllGather要求数据交换全部结束后结果才完整：Host侧在`aclrtSynchronizeStream`后，需通过rank间同步（如TCP控制通道Barrier）确认所有rank的Kernel均已完成，再校验或使用AllGather结果。

## 延迟提交与批量优化

### 延迟提交

普通接口默认每次调用立即提交；提交任务时设置`commit = false`可延迟提交，减少提交开销。SIMT路径没有`Commit`接口：`commit = false`的任务停留在发送队列中，由后续任意一次`commit = true`的任务提交时带出（其门铃发布覆盖整批任务的生产者索引），因此同一批延迟任务必须由同一个lane提交：

  ```cpp
  // 前置条件：channel为本端通道句柄，dst为远端注册内存中的目的GM地址，src为本端源GM地址，len为写入长度
  // 延迟提交：只添加任务，不提交
  ret = hcomm.WriteNbi<false>(channel, dst, src, len);
  // 由本次立即提交带出整批延迟任务
  ret = hcomm.WriteNbi<true>(channel, dstLast, srcLast, lenLast);
  // 等待该通道全部任务完成
  ret = hcomm.Drain(channel);
  ```

### 批量提交

需要以批量方式提交通信任务时，可使用批量接口。批量接口通过`MakeBatchHandle`创建的批量句柄添加任务，通过`BatchCommit`统一提交，其UB工作区由`MakeBatchHandle`绑定（至少256字节，即128字节共享上下文与128字节发布者DWQE（Direct WQE，直接写入硬件门铃窗口的工作队列元素）镜像，工作区大小不随Group规模增长），不通过`Init`传入。SIMT批量分为单lane批量与多lane协同批量两种形态：单lane批量当前每批仅支持提交一个任务请求，需要提交多个任务时逐个添加并逐批提交；多lane协同批量每批由group内每个lane各添加一个任务，每批任务数等于lane数。

主要约束：

- 通过批量句柄提交的请求，目标地址必须位于批量句柄所属通道的远端注册内存内，由调用方保证。
- 同一批次内追加的请求必须使用相同的WQE宽度（即占用相同的任务槽位数量，如写通知/原子任务占2个任务槽位，普通写/读任务占1个），混用宽度不受支持。
- 同一批量句柄从创建到失效必须使用同一Group（成员、lane rank与size保持不变）和同一UB工作区；未传Group的重载用于单lane句柄，传入Group的句柄按多lane协同路径处理（即使size为1），每次添加任务与`BatchCommit`均须传入该Group。
- 添加任务的接口返回失败时，不得对该批次调用`BatchCommit`；未添加任何任务的空批次提交将失败。
- 通道在同一时刻只能被一个存活的BatchHandle独占，存活期间不能在该通道上使用普通接口提交任务。

#### 单lane批量

单lane通过批量句柄先入队再统一发布。当前单lane批量每批仅支持提交一个任务请求：添加一个任务后即调用`BatchCommit`提交，需要提交多个任务时逐个添加并逐批提交，批次间复用同一句柄：

  ```cpp
  // 准备批量工作区：至少256字节（128字节共享上下文 + 128字节发布者DWQE镜像），起始地址按16字节对齐
  constexpr uint32_t WORKSPACE_BYTES = 256U;
  alignas(16) __ubuf__ uint8_t batchWorkspace[WORKSPACE_BYTES];

  auto batchHandle = hcomm.MakeBatchHandle(channel, batchWorkspace, WORKSPACE_BYTES);
  if (batchHandle.context == nullptr) {
      return;
  }

  // 前置条件：remoteDst/localSrc/len为写任务的目的/源GM地址与长度，由调用方按业务准备
  // 每批仅添加一个任务并提交，逐批复用同一句柄
  ret = hcomm.WriteNbi(batchHandle, remoteDst, localSrc, len);
  ret = hcomm.BatchCommit(batchHandle);
  ret = hcomm.WriteNbi(batchHandle, remoteDst2, localSrc2, len2);
  ret = hcomm.BatchCommit(batchHandle);
  // 等待该批量句柄累计提交的任务完成
  ret = hcomm.Drain(batchHandle);
  ```

#### 多lane协同批量

group内每个lane各构造一个请求并添加到当前批次，`BatchCommit`为协作调用（group内所有lane均须调用），由发布lane统一发布整批，每批任务数等于lane数。group为协作组对象，由头文件`simt_api/cooperative_groups.h`提供（如通过`cooperative_groups::this_thread_block()`获取当前线程块组，`__simt_vf__`函数需以多lane形态下发）。Hcomm对象为lane私有，Group内每个lane各自持有，并在每次添加任务与`BatchCommit`时显式传入group：

  ```cpp
  // group来自simt_api/cooperative_groups.h，通过this_thread_block()等接口构造
  auto group = cooperative_groups::this_thread_block();

  // 前置条件：remoteDstForLane/localSrcForLane/len为每lane各自写任务的目的/源GM地址与长度
  // 以下调用由group所有lane参与，每lane构造自己的一个请求
  auto batchHandle = hcomm.MakeBatchHandle(channel, batchWorkspace, WORKSPACE_BYTES, group);
  if (batchHandle.context == nullptr) {
      return;
  }
  ret = hcomm.WriteNbi(batchHandle, remoteDstForLane, localSrcForLane, len, group);
  ret = hcomm.BatchCommit(batchHandle, group);
  // 所有lane同步后，由一个lane调用Drain，Drain不接收group
  group.sync();
  if (group.thread_rank() == 0U) {
      ret = hcomm.Drain(batchHandle);
  }
  ```

完整流程参见样例[simt_hcomm_urma](../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md)（接口功能）、[simt_hcomm_urma_perftest](../../../examples/aicore/hcomm/05_simt_hcomm_urma_perftest/README.md)（发送性能）与[simt_hcomm_urma_batch_perf](../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md)（批量提交方式）。

## 注意事项

- SIMT路径仅支持`COMM_PROTOCOL_UB_CTP`协议，Host侧通道协议与Kernel侧`AscendC::simt::Hcomm`模板协议必须一致。
- `Init`的两个入参未使用，固定传`nullptr`和`0`，无需申请UB临时工作区。
- 一个通道必须由一个lane驱动：普通接口由单个lane调用，同一批任务（含延迟任务）必须由同一个lane提交，其余lane直接返回。
- 缓冲区地址按通道缓冲区表下标解析：本地表下标0为通信域的HcclBuffer，显式注册的内存从下标1开始编排；远端下标取`HcclChannelGetRemoteMems`返回的内存表中按tag匹配到的下标。不能直接传`HcclCommMemReg`的`memHandles`数组下标，否则会解析到HcclBuffer。
- SIMT路径没有`Commit`接口，`commit = false`的任务由后续`commit = true`的任务提交时带出。
- 批量提交：同一批次内追加的请求必须使用相同的WQE宽度，混用宽度不受支持；单lane批量当前每批仅支持提交一个任务请求，多lane协同批量每批由group内每个lane各添加一个任务，`BatchCommit`由所有lane协作调用。
- SQ（Send Queue，发送队列）占用：延迟提交的`WriteNbi`/`ReadNbi`/`WriteValueNbi`占1个任务槽位（延迟提交任务会在自身槽位之外额外预留2个任务槽位，供发布该批的立即提交DWQE使用，容量规划时需计入）；立即提交的`WriteNbi`/`ReadNbi`/`WriteValueNbi`占2个任务槽位；写通知与原子任务无论提交方式均占2个。连续占用的任务槽位总数不得超过通道的任务提交容量（SQ深度sqDepth），需在容量耗尽前由`commit`为`true`的任务提交。
- 同一通道不能同时由SIMT的`AscendC::simt::Hcomm`与SIMD的`AscendC::Hcomm`驱动。
- 单次`WriteNbi`/`ReadNbi`的数据长度必须不超过256MB；立即提交的普通接口`WriteNbi`/`ReadNbi`的数据长度`len`至少为2字节。
