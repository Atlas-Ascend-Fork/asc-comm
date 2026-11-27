# 基于CommRW的AIV直驱通信算子开发

## 概述

CommRW是覆盖AI Core与AI CPU侧的点对点通信接口，其中AI Core侧通过`asc::asccomm`模板选择通信协议，提供`write_nbi`/`read_nbi`/`write_value_nbi`/`write_with_notify_nbi`/`write_reduce_nbi`（带归约的写任务）/`atomic_faa_nbi`/`atomic_cas_nbi`等单边数据面能力及`commit`/`drain`提交与完成语义。AI Core侧CommRW基于显式的`ChannelHandle`通信通道与注册内存工作：开发者通过HCCL（Huawei Collective Communication Library，华为集合通信库）接口获取对端链路并创建通道，Kernel侧使用对端注册内存的远端地址直接提交读写任务。

本节以AllGather为例，介绍CommRW通信算子的完整开发流程。AllGather将每个rank的本端分片聚合到通信域内所有rank：各rank通过`write_nbi`将本端分片写入其余每个对端缓冲区的对应偏移，通信完成后所有rank的缓冲区均持有按rank_id排列的全量数据。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 通信协议 | 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`，各协议支持的具体接口参见[AI Core CommRW API](../../api/aicore/asccomm/README.md) |
| 通信引擎 | `COMM_ENGINE_AIV` |

<!-- npu="950" id1 -->
> [!NOTE]说明
> 仅Ascend 950PR&950DT系列产品支持实现CommRW算子。单卡环境仅支持编译验证，实际运行需要至少2张NPU。
<!-- end id1 -->

## 总体流程

CommRW通信算子开发分为控制面资源准备和数据面通信执行两阶段。

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
  uint32_t rank;      // 当前rank_id
  uint32_t nranks;    // rank总数
  aclInit(nullptr);
  aclrtSetDevice(static_cast<uint32_t>(rank));
  aclrtStream stream = nullptr;
  aclrtCreateStream(&stream);
  ```

### 创建通信域

通信域内所有rank必须共用同一份root_info建域（参见[`HcclGetRootInfo`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_mgr_c/HcclGetRootInfo.md)与[`HcclCommInitRootInfo`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_mgr_c/HcclCommInitRootInfo.md)）。CommRW流程需通过`HcclCommConfig`创建通信域，并设置`hcclWorldRankID`为当前全局rank，对应接口参见[`HcclCommConfigInit`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_mgr_c/HcclCommConfigInit.md)与[`HcclCommInitRootInfoConfig`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_mgr_c/HcclCommInitRootInfoConfig.md)。

样例工程通过以rank 0为中心的TCP socket控制通道交换root_info，参见`examples/aicore/utils/rank_sync.h`与`examples/aicore/utils/hccl_comm_init.h`。也可采用文件交换等其他方式：由root节点将root_info写入共享路径下的文件，其他rank等待文件就绪后读取同一份root_info。

  ```cpp
  // 控制通道endpoint（由命令行传入），形如"tcp://ip:port"
  std::string ctrl_endpoint;
  // 建立TCP控制通道：rank 0监听endpoint，其余rank连接
  examples::rank_sync_context sync = rank == 0U ? examples::rank_sync_context::make_listen(ctrl_endpoint, nranks) :
                                                examples::rank_sync_context::make_connect(rank, nranks, ctrl_endpoint);

  // 配置通信域创建参数
  HcclCommConfig config;
  HcclCommConfigInit(&config);
  config.hcclWorldRankID = rank;

  // 生成root节点的rank标识信息：由root节点生成，通信域内所有rank必须共用同一份root_info
  HcclRootInfo root_info = {};
  if (rank == 0) {
      HcclGetRootInfo(&root_info);
  }
  // 通过控制通道AllGather交换root_info，所有rank取用rank 0的root_info
  std::vector<HcclRootInfo> all_root_info(nranks);
  sync.allgather(examples::TAG_ROOT_INFO, &root_info, sizeof(root_info), all_root_info.data());
  // 所有rank使用同一份root_info创建通信域
  HcclComm hccl_comm;
  HcclCommInitRootInfoConfig(nranks, &all_root_info[0], rank, &config, &hccl_comm);
  ```

### 分配内存并注册通信内存

AllGather场景下，通信缓冲区需容纳`nranks`个分片：本端分片预置于`rank_id * SHARD_SIZE`偏移，作为Kernel侧写入各对端的源数据；该缓冲区同时作为AllGather结果缓冲区，接收其余rank写入的分片。

分配通信缓冲区，并通过[`HcclCommMemReg`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclCommMemReg.md)将本端缓冲区注册为携带tag的通信内存，供对端通过通道识别与访问：

  ```cpp
  constexpr uint64_t SHARD_SIZE = 4096U;               // 单个分片大小
  const uint64_t COMM_BUF_SIZE = SHARD_SIZE * nranks;  // 容纳nranks个分片
  constexpr const char* MEMORY_TAG = "SHARE_BUF";

  // 分配本端通信缓冲区
  void* dev_buf = nullptr;
  aclrtMalloc(&dev_buf, COMM_BUF_SIZE, ACL_MEM_MALLOC_HUGE_FIRST);

  // 初始化本端分片：写入本端rank_id对应的分片位置
  std::vector<uint8_t> host_buffer(static_cast<size_t>(COMM_BUF_SIZE), 0U);
  for (uint64_t i = 0; i < SHARD_SIZE; i++) {
      host_buffer[rank * SHARD_SIZE + i] = static_cast<uint8_t>(rank);  // 按业务语义填充本端分片
  }
  aclrtMemcpy(dev_buf, COMM_BUF_SIZE, host_buffer.data(), COMM_BUF_SIZE, ACL_MEMCPY_HOST_TO_DEVICE);

  // 注册通信内存：tag用于对端识别，需各rank统一约定
  CommMem registered_memory = {};
  registered_memory.type = COMM_MEM_TYPE_DEVICE;
  registered_memory.addr = dev_buf;
  registered_memory.size = COMM_BUF_SIZE;
  HcclMemHandle mem_handle;
  HcclCommMemReg(hccl_comm, MEMORY_TAG, &registered_memory, &mem_handle);
  ```

### 获取对端链路并创建通道

通过包含当前rank的拓扑模型获取到对端的通信链路，配置通道描述并创建通道，涉及接口参见[`HcclRankGraphGetLayers`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_opdev/control_plane_api/topo_info_query/HcclRankGraphGetLayers.md)、[`HcclRankGraphGetLinks`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_opdev/control_plane_api/topo_info_query/HcclRankGraphGetLinks.md)、[`HcclChannelDescInit`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelDescInit.md)与[`HcclChannelAcquire`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)。通道为点对点资源，每个对端需单独创建通道；AllGather场景下本端与其余`nranks - 1`个对端各建一条通道：

  ```cpp
  // 查询包含当前rank的拓扑层级编号列表以及拓扑层级数量
  uint32_t* network_layers = nullptr;
  uint32_t network_layer_count = 0;
  HcclRankGraphGetLayers(hccl_comm, &network_layers, &network_layer_count);

  std::vector<ChannelHandle> channels;  // 每个对端一条通道
  for (uint32_t offset = 1U; offset < nranks; offset++) {
      uint32_t peer_rank = (rank + offset) % nranks;

      // 获取到对端的链路
      CommLink* links = nullptr;
      uint32_t link_count = 0;
      HcclRankGraphGetLinks(hccl_comm, network_layers[0], rank, peer_rank, &links, &link_count);

      // 初始化通道描述：远端rank、通道协议、两端endpoint与本端注册内存
      HcclChannelDesc channel_desc;
      HcclChannelDescInit(&channel_desc, 1U);
      channel_desc.remoteRank = peer_rank;
      // RoCE（RDMA over Converged Ethernet，基于以太网的RDMA协议）适配：将本处与下方链路匹配的协议常量同步改为COMM_PROTOCOL_ROCE（需组网提供RoCE链路）
      channel_desc.channelProtocol = COMM_PROTOCOL_UB_CTP;
      channel_desc.memHandles = &mem_handle;
      channel_desc.memHandleNum = 1U;
      for (uint32_t i = 0; i < link_count; i++) {
          if (links[i].linkAttr.linkProtocol == COMM_PROTOCOL_UB_CTP) {
              channel_desc.localEndpoint = links[i].srcEndpointDesc;
              channel_desc.remoteEndpoint = links[i].dstEndpointDesc;
              break;
          }
      }

      // 获取通道句柄
      ChannelHandle channel = 0;
      HcclChannelAcquire(hccl_comm, COMM_ENGINE_AIV, &channel_desc, 1U, &channel);
      channels.emplace_back(channel);
  }
  ```

### 获取对端注册内存

通过通道查询对端注册的通信内存，按tag匹配得到远端缓冲区地址（为聚焦调用流程，Host侧示例省略返回值检查与错误处理），参见[`HcclChannelGetRemoteMems`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetRemoteMems.md)。Kernel侧基于该地址加偏移计算远端写入位置：

  ```cpp
  std::vector<uint64_t> remote_bufs(channels.size(), 0U);
  for (uint32_t i = 0; i < channels.size(); i++) {
      uint32_t remote_memory_count = 0;
      CommMem* remote_memories = nullptr;
      char** memory_tags = nullptr;
      HcclChannelGetRemoteMems(hccl_comm, channels[i], &remote_memory_count, &remote_memories, &memory_tags);

      for (uint32_t j = 0; j < remote_memory_count; j++) {
          if (memory_tags[j] != nullptr && strcmp(memory_tags[j], MEMORY_TAG) == 0) {
              remote_bufs[i] = reinterpret_cast<uint64_t>(remote_memories[j].addr);
              break;
          }
      }
  }
  ```

### 下发句柄到Kernel

将各对端`channel`、本端缓冲区地址`dev_buf`与对端缓冲区地址`remote_bufs`组装为Host与Kernel共享的上下文（结构定义参见[Kernel侧通信执行](#kernel侧通信执行)，需在Host与Kernel共享的头文件中定义）后下发：

  ```cpp
  void* dev_context = nullptr;
  aclrtMalloc(&dev_context, sizeof(all_gather_context), ACL_MEM_MALLOC_HUGE_FIRST);

  all_gather_context context = {};
  context.local_buffer_addr = reinterpret_cast<uint64_t>(dev_buf);
  context.rank_id = rank;
  context.nranks = nranks;
  context.peer_num = static_cast<uint32_t>(channels.size());
  for (uint32_t i = 0; i < channels.size(); i++) {
      context.channel_handles[i] = channels[i];
      context.remote_buffer_addrs[i] = remote_bufs[i];
  }
  aclrtMemcpy(dev_context, sizeof(context), &context, sizeof(context), ACL_MEMCPY_HOST_TO_DEVICE);

  all_gather_kernel<<<1U, nullptr, stream>>>(dev_context);
  ```

### 销毁通信资源

通信任务完成后，销毁通信资源（参见[`HcclCommDestroy`](https://gitcode.com/cann/hcomm/blob/9.3.0/docs/zh/api_ref/comm_mgr_c/HcclCommDestroy.md)）。样例工程中通道与注册内存随通信域统一释放，参见样例[asccomm_urma_write_read_nbi](../../../../examples/aicore/asccomm/01_asccomm_urma_write_read_nbi/README.md)：

  ```cpp
  // 销毁通信域（通道与注册内存随通信域统一释放）
  HcclCommDestroy(hccl_comm);
  // 释放申请的内存
  aclrtFree(dev_context);
  aclrtFree(dev_buf);
  // 去初始化
  aclrtDestroyStream(stream);
  aclrtResetDevice(static_cast<uint32_t>(rank));
  aclFinalize();
  ```

## Kernel侧通信执行

Kernel入口函数如下，通道句柄与本端、对端缓冲区地址经`all_gather_context`上下文以入参形式传入，后续步骤在该函数内展开。Host侧与Kernel侧的协议常量必须一致（如均为`COMM_PROTOCOL_UB_CTP`）：

  ```cpp
  #include "asccomm/asccomm.h"

  constexpr uint32_t MAX_PEERS = 16U;  // 对端数上限，按组网规模调整
  constexpr uint64_t SHARD_SIZE = 4096U;

  struct all_gather_context {
      uint64_t local_buffer_addr;
      uint64_t channel_handles[MAX_PEERS];
      uint64_t remote_buffer_addrs[MAX_PEERS];
      uint32_t rank_id;
      uint32_t nranks;
      uint32_t peer_num;  // 对端数量，即nranks - 1
  };

  extern "C" __vector__ __global__ __aicore__ void all_gather_kernel(GM_ADDR context)
  {
      // 后续Kernel侧通信执行任务在该函数内展开
  }
  ```

Kernel侧通信接口分为普通接口和批量接口两类，均通过`asc::asccomm`对象调用：

- 普通接口：直接以`ChannelHandle`为入参，每次调用提交任务，需先调用`init`初始化UB（Unified Buffer，统一缓冲区）临时工作区。
- 批量接口：以批量句柄为入参，多个任务组装后统一提交，减少提交开销，UB工作区由`make_batch_handle`绑定。

### 预处理

申请UB临时工作区（不小于512字节），创建`asc::asccomm`对象并调用`init`初始化工作区，随后解析上下文中的通道与地址信息：

  ```cpp
  // 初始化UB临时工作区大小：不得小于512字节
  constexpr uint32_t ASCCOMM_WORKSPACE_SIZE = 512U;

  // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
  alignas(32) __ubuf__ uint8_t workspace[ASCCOMM_WORKSPACE_SIZE];

  // 创建asccomm对象并初始化普通接口使用的临时工作区
  // RoCE适配：模板协议改为asc::COMM_PROTOCOL_ROCE，与Host侧通道协议一致；
  asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
  int32_t ret = asccomm.init(workspace, ASCCOMM_WORKSPACE_SIZE);

  // 解析上下文：本端缓冲区地址与rank信息
  __gm__ all_gather_context* ctx = reinterpret_cast<__gm__ all_gather_context*>(context);
  __gm__ uint8_t* local_buf = reinterpret_cast<__gm__ uint8_t*>(ctx->local_buffer_addr);
  uint32_t rank_id = ctx->rank_id;
  ```

### 提交通信任务

AllGather：遍历全部对端通道，将本端分片（位于`rank_id * SHARD_SIZE`偏移）写入每个对端缓冲区的相同偏移。通信完成后，每个rank的缓冲区在`k * SHARD_SIZE`偏移处持有rank k的分片，即为AllGather结果：

  ```cpp
  for (uint32_t i = 0U; i < ctx->peer_num; i++) {
      asc::ChannelHandle channel = ctx->channel_handles[i];
      __gm__ uint8_t* remote_buf = reinterpret_cast<__gm__ uint8_t*>(ctx->remote_buffer_addrs[i]);
      __gm__ uint8_t* remote_dst = remote_buf + static_cast<uint64_t>(rank_id) * SHARD_SIZE;
      __gm__ uint8_t* local_src = local_buf + static_cast<uint64_t>(rank_id) * SHARD_SIZE;
      ret = asccomm.write_nbi(channel, remote_dst, local_src, SHARD_SIZE);
  }
  ```

通信任务数量较多时，可设置`commit = false`延迟提交以减少提交开销，参见[延迟提交与批量优化](#延迟提交与批量优化)。

### 等待完成

逐通道调用`drain`阻塞等待该通道全部任务完成：

  ```cpp
  for (uint32_t i = 0U; i < ctx->peer_num; i++) {
      ret = asccomm.drain(ctx->channel_handles[i]);
  }
  ```

`drain`仅保证本端已提交通道的任务完成。AllGather要求数据交换全部结束后结果才完整：Host侧在`aclrtSynchronizeStream`后，需通过rank间同步（如TCP控制通道Barrier）确认所有rank的Kernel均已完成，再校验或使用AllGather结果；若需要设备侧感知数据到达，可使用`write_with_notify_nbi`与`atomic_faa_nbi`，参见[通知与原子操作](#通知与原子操作)。

## 通知与原子操作

`write_with_notify_nbi`在写数据后向对端notify地址写入通知值，可用于对端感知数据到达；`atomic_faa_nbi`/`atomic_cas_nbi`对远端注册内存执行原子累加与比较交换。上述接口仅支持`COMM_PROTOCOL_UB_CTP`协议：

  ```cpp
  // channel为本端通道句柄，remote_dst/local_src为远端目的与本端源GM地址，len为写入长度
  // notify_addr、remote_counter、remote_value为对端注册内存中的地址，fetch_addr为本端取回旧值的地址
  // notify_val/add_val/compare_val/swap_val为业务约定的通知值与原子操作数
  // 写数据并向对端notify_addr写入notify_val
  ret = asccomm.write_with_notify_nbi(channel, remote_dst, local_src, len, notify_addr, notify_val);
  // 远端原子累加
  ret = asccomm.atomic_faa_nbi<uint64_t>(channel, remote_counter, fetch_addr, add_val);
  // 远端原子比较交换
  ret = asccomm.atomic_cas_nbi<uint64_t>(channel, remote_value, fetch_addr, compare_val, swap_val);
  ```

上述接口可对应实现[执行模型](../programming_model/execution_model.md)同步机制章节中的本端同步与远端同步：

### 本端同步

`drain`即本端同步：阻塞等待指定通道上已提交的全部通信任务完成（内部轮询完成队列CQ）。以远端读为例，提交读任务后必须先完成本端同步再访问读取结果，否则可能读到未更新完毕的数据：

  ```cpp
  // 前置条件：channel为本端通道句柄，remote_src为对端注册内存中的源GM地址，local_dst为本端目的GM地址，len为读取长度
  ret = asccomm.read_nbi(channel, local_dst, remote_src, len);  // 单边读：将对端src数据读入本端dst
  ret = asccomm.drain(channel);                              // 本端同步：等待该通道已提交任务完成
  // 本端同步完成，local_dst中的数据可安全访问
  ```

### 远端同步

`drain`仅确认本端任务完成，无法感知对端发送给本端的数据是否已就绪。需要设备侧感知数据到达时，采用点对点远端同步：对端在写数据后发送同步信号，本端轮询该信号，确认后再访问数据。

写侧在写数据的同时携带同步信号：`write_with_notify_nbi`一次调用完成写数据与写通知，`notify_val`按同步轮次递增；计数型信号可改为`write_nbi`写数据后追加`atomic_faa_nbi`对对端计数器原子累加（调用形式参见上文）。任务提交后调用`drain`等待完成：

  ```cpp
  // notify_addr为对端（读侧）注册内存中的notify地址
  ret = asccomm.write_with_notify_nbi(channel, remote_dst, local_src, len, notify_addr, notify_val);
  ret = asccomm.drain(channel);
  ```

读侧轮询本端notify地址直至达到预期值，确认对端数据已完整写入。AIV读取GM需通过c_api数据搬运接口将数据搬运到UB后再读取，参见样例[03_asccomm_urma_one_multi_path](../../../../examples/aicore/asccomm/03_asccomm_urma_one_multi_path/README.md)中的`ubmem_data_copy_kernel`：

  ```cpp
  // notify_addr为本端注册内存中的notify地址（由写侧写入），需32字节对齐
  // expected_notify_val为本端等待的预期值，按同步轮次递增
  // 需包含头文件<c_api/vector_datamove/vector_datamove.h>与<c_api/sync/sync.h>
  constexpr uint32_t SYNC_COUNT = 4U;  // 搬运量32字节对齐，一次读取4个uint64_t
  __ubuf__ uint64_t sync_workspace[SYNC_COUNT];
  __gm__ uint64_t* notify_gm = reinterpret_cast<__gm__ uint64_t*>(notify_addr);
  uint64_t observed = 0U;
  do {
      asc_copy_gm2ub_align(sync_workspace, notify_gm, SYNC_COUNT * sizeof(uint64_t));
      // 等待搬运完成后，再从UB读取notify值
      asc_sync_mte2(0);
      observed = sync_workspace[0];
  } while (observed < expected_notify_val);
  // 点对点同步完成，可安全访问对端写入的数据
  ```

屏障同步为集合级远端同步，CommRW未提供对应接口：AllGather等集合语义的前后屏障由Host侧rank间同步完成（参见[等待完成](#等待完成)）；设备侧确需屏障时，可基于`atomic_faa_nbi`对约定rank注册内存中的共享计数器累加，再按点对点同步轮询计数值直至全部rank抵达。屏障同步引入全局等待，应避免滥用；需要远端同步时优先采用点对点同步。

## 延迟提交与批量优化

### 延迟提交

普通接口默认每次调用立即提交；提交任务时设置`commit = false`可延迟到显式调用`commit`统一提交，减少提交开销：

  ```cpp
  // 前置条件：channel为本端通道句柄，dst为远端注册内存中的目的GM地址，src为本端源GM地址，len为写入长度
  // 延迟提交：只添加任务，不提交
  ret = asccomm.write_nbi<false>(channel, dst, src, len);
  // 显式统一提交
  ret = asccomm.commit(channel);
  // 等待该通道全部任务完成
  ret = asccomm.drain(channel);
  ```

`commit`模板参数设为`false`时，积攒的延迟提交任务占用的槽位数之和不得超过通道的任务提交容量（发送队列深度`sqDepth`），需在SQ耗尽前通过`commit`或自动commit提交积攒的任务，否则后续任务提交将因SQ溢出而失败。

### 批量提交

需要组装多个任务统一提交时，可使用批量接口。批量接口通过批量句柄组装多个任务并统一提交，不需要调用`init`，其UB工作区由`make_batch_handle`绑定。按通道数量分为单通道批量与多通道批量两种形态。`COMM_PROTOCOL_UB_CTP`协议下同一批次可混合添加读、写和写通知任务；`COMM_PROTOCOL_ROCE`协议下仅支持批量读和写任务。

#### 单通道批量

需要向同一通道提交多个任务时，通过批量句柄组装多个任务并统一提交：

  ```cpp
  // 前置条件：channel为本端通道句柄，remote_addr用于选择远端注册内存，
  // remote_write_dst/local_write_src/write_len为写任务的目的/源GM地址与长度，
  // local_read_dst/remote_read_src/read_len为读任务的目的/源GM地址与长度，均由调用方按业务准备
  // 批量工作区长度：至少64字节，按64字节任务槽划分
  constexpr uint32_t batch_buffer_len = 128U;
  // 声明批量UB工作区；alignas(32)保证起始地址按32字节对齐
  alignas(32) __ubuf__ uint8_t batch_workspace[batch_buffer_len];
  // 创建批量句柄并绑定批量工作区，remote_addr用于选择远端注册内存
  auto batch_handle = asccomm.make_batch_handle(channel, batch_workspace, batch_buffer_len, remote_addr);

  // 添加读、写、写通知任务，同一批次可混合使用
  ret = asccomm.write_nbi(batch_handle, remote_write_dst, local_write_src, write_len);
  ret = asccomm.read_nbi(batch_handle, local_read_dst, remote_read_src, read_len);
  // 统一提交当前批次
  ret = asccomm.batch_commit(batch_handle);
  // 等待该批量句柄提交的任务完成
  ret = asccomm.drain(batch_handle);
  ```

`remote_addr`用于指定本批任务访问的远端注册内存：`COMM_PROTOCOL_UB_CTP`协议下该参数不生效，固定使用第一个远端注册buffer；`COMM_PROTOCOL_ROCE`协议必须显式传入（多SGE场景见[多SGE批量读写](#多sge批量读写)）。`batch_commit`提交成功后，可以复用批量句柄和工作区继续添加并提交下一批任务。

完整流程参见样例[asccomm_roce_batch_write_read](../../../../examples/aicore/asccomm/08_asccomm_roce_batch_write_read/README.md)（COMM_PROTOCOL_ROCE协议）。

#### 多通道批量

需要向多个对端批量提交任务时，Host侧通过`make_multi_channel_handle`创建多通道句柄。使用HCCL通信域管理资源时，采用通信域重载，创建的资源随通信域销毁自动释放：

  ```cpp
  #include "asccomm/asccomm_host.h"

  // channel_descs/channel_num：按本地endpoint对通道描述分组后的数组及其数量，由调用方准备
  // shared_queue_tag：多通道共享提交队列的tag，由调用方构造
  multi_channel_handle multi_channel = 0U;
  HcclResult ret = make_multi_channel_handle(
      hccl_comm, shared_queue_tag, channel_descs, channel_num, &multi_channel);
  ```

不使用HCCL通信域时，传入Hcomm Endpoint和`HcommChannelDesc`调用另一个重载。Kernel执行并同步完成后，需要先销毁多通道句柄，再销毁Endpoint：

  ```cpp
  // endpointHandle：Hcomm Endpoint句柄，由Endpoint创建接口获得
  HcommResult ret = make_multi_channel_handle(
      endpointHandle, channel_descs, channel_num, &multi_channel);

  // Kernel执行并同步完成后，再销毁句柄和Endpoint
  ret = destroy_multi_channel_handle(multi_channel);
  // HcommEndpointDestroy：销毁Hcomm Endpoint的外部接口，由asccomm外部组件提供（与make_multi_channel_handle的Endpoint重载配套）
  ret = HcommEndpointDestroy(endpointHandle);
  ```

AI Core侧通过`get_handle_ref`选择逻辑通道（对应`channel_descs`数组下标）和远端注册内存，再通过多通道批量句柄添加、提交并等待任务：

  ```cpp
  // remote_buffers：各逻辑通道对应的对端注册内存地址；local_buffer/data_len：本端数据地址与长度
  auto multi_batch_handle = asccomm.make_batch_handle(
      static_cast<asc::multi_channel_handle>(multi_channel), batch_workspace, batch_buffer_len);

  for (uint32_t channel_idx = 0U; channel_idx < channel_num; ++channel_idx) {
      auto& peer_batch_handle = asccomm.get_handle_ref(multi_batch_handle, channel_idx, remote_buffers[channel_idx]);
      ret = asccomm.write_nbi(peer_batch_handle, remote_buffers[channel_idx], local_buffer, data_len);
  }
  ret = asccomm.batch_commit(multi_batch_handle);
  ret = asccomm.drain(multi_batch_handle);
  ```

切换逻辑通道或远端注册内存时，需要重新调用`get_handle_ref`；对同一个多通道批量句柄重复调用时，返回的引用指向同一个内层batch_handle，并随最近一次调用更新其逻辑通道和远端注册内存选择。

完整流程参见样例[asccomm_urma_batch_write](../../../../examples/aicore/asccomm/02_asccomm_urma_batch_write/README.md)。

## 多SGE批量读写

支持多SGE（Scatter Gather Element）批量读写：单次`write_nbi`/`read_nbi`通过`buf_desc`数组描述多个本端离散数据段，对远端一段连续内存聚合读写，适合源数据分块离散存放的场景。

多SGE由`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议支持，且仅通过批量接口实现：普通接口`write_nbi`/`read_nbi`不支持`buf_desc`入参，多SGE任务需经`make_batch_handle`创建的批量句柄提交。`src_descs`/`dst_descs`数据段数量在`COMM_PROTOCOL_UB_CTP`协议下为1-6、`COMM_PROTOCOL_ROCE`协议下为1-24。`COMM_PROTOCOL_ROCE`协议下创建批量句柄时需同时绑定远端与本端注册内存：`local_addr`须为本端注册内存中的地址，用于绑定本端内存保护信息；`src_descs`/`dst_descs`中各数据段的地址须位于绑定注册内存范围内、各数据段`len`及累加总长度均须小于2GB（2 * 1024 * 1024 * 1024字节）。`COMM_PROTOCOL_UB_CTP`协议下`local_addr`不生效，`remote_addr`固定使用第一个远端注册buffer（参见[单通道批量](#单通道批量)）。各数据段地址与长度的合法性由调用方保证，句柄创建时不校验各数据段地址，违规将在任务提交或执行阶段暴露。

  ```cpp
  // 创建批量句柄：同时绑定远端注册内存与本端注册内存
  auto batch_handle = asccomm.make_batch_handle(channel, batch_workspace, batch_buffer_len, remote_addr, local_addr);

  // 单次写：将本端SGE_NUM个离散数据段写入远端连续区间remote_dst（SGE_NUM/SEGMENT_STRIDE/SEGMENT_SIZE为业务约定的段数量、步长与段长，remote_dst需位于remote_addr所在注册内存内）
  asc::buf_desc src_descs[SGE_NUM];
  for (uint32_t i = 0; i < SGE_NUM; i++) {
      src_descs[i].addr = local_addr + i * SEGMENT_STRIDE;  // 本端离散段地址
      src_descs[i].len = SEGMENT_SIZE;
  }
  ret = asccomm.write_nbi(batch_handle, remote_dst, src_descs, SGE_NUM);

  // 单次读：从远端连续区间remote_src读回本端SGE_NUM个离散数据段（remote_src需位于remote_addr所在注册内存内）
  asc::buf_desc dst_descs[SGE_NUM];
  // 按相同方式填充dst_descs
  ret = asccomm.read_nbi(batch_handle, dst_descs, SGE_NUM, remote_src);

  // 提交并等待本批任务完成
  ret = asccomm.batch_commit(batch_handle);
  ret = asccomm.drain(batch_handle);
  ```

完整流程参见样例[asccomm_roce_multi_sge](../../../../examples/aicore/asccomm/09_asccomm_roce_multi_sge/README.md)。

## 多AI Core共享通道

多个AIV（AI Vector，向量核）共享同一个`COMM_PROTOCOL_UB_CTP`通道时，需通过[lock](../../api/aicore/asccomm/lock.md)和[unlock](../../api/aicore/asccomm/unlock.md)保护会更新通道状态的访问。每个AI Core可使用独立的asccomm对象和UB临时工作区，但传入相同的`ChannelHandle`。锁不可重入，每次成功`lock`后必须由同一AI Core调用`unlock`，错误退出路径也需要释放锁；接口仅同步同一设备上共享同一通道的AI Core：

  ```cpp
  int32_t lock_ret = asccomm.lock(channel);
  if (lock_ret == 0) {
      int32_t op_ret = asccomm.write_nbi(channel, dst, src, len);
      // 即使通信接口返回失败，也必须释放已经成功获取的锁
      int32_t unlock_ret = asccomm.unlock(channel);
      // 分别处理op_ret和unlock_ret
  }
  ```

## 注意事项

- 普通接口需先调用`init`提供UB临时工作区，最小512字节；使用`__ubuf__ uint8_t*`时起始地址按32字节对齐。
- Host侧通道协议（`HcclChannelDesc.channelProtocol`）与Kernel侧`asccomm`模板协议必须一致。
- 通道为点对点资源，每个对端需单独`HcclChannelAcquire`；环形多对端场景建议按rank奇偶错开建链顺序，避免循环等待；`nranks == 2`时前后对端相同，可复用同一条通道。
- 单次`write_nbi`/`read_nbi`的数据长度：`COMM_PROTOCOL_UB_CTP`协议下必须不超过256MB，`COMM_PROTOCOL_ROCE`协议下必须小于2GB（2 * 1024 * 1024 * 1024字节）。
- `write_with_notify_nbi`、`write_value_nbi`、`write_reduce_nbi`、`atomic_faa_nbi`和`atomic_cas_nbi`仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 原子操作的数据类型仅支持`int32_t`、`uint32_t`、`int64_t`和`uint64_t`。
