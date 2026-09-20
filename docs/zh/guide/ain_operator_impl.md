# 基于Ain的通信算子开发指南

## 概述

Ain（Ascend-Initiated Networking）是AI Core侧单边通信接口模板，提供`Put`/`Get`/`PutValue`/`Signal`等单边通信能力及`Flush`/`WaitSignal`/`Sync`同步能力。开发者无需在Kernel侧直接维护远端物理地址，Ain会根据通信team、对端rank和对称window句柄自动解析通信通道与通信内存地址。

本指南以AllGather为例，介绍Ain通信算子的完整开发流程。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 通信协议 | `COMM_PROTOCOL_UB_CTP` |
| 通信引擎 | `COMM_ENGINE_AIV` |

<!-- npu="950" id1 -->
> [!NOTE]说明
> 仅Ascend 950PR&950DT系列产品支持实现Ain算子。单卡环境仅支持编译验证，实际运行需要至少2张NPU。
<!-- end id1 -->

## 总体流程

Ain通信算子开发分为控制面资源准备和数据面通信执行两阶段。

控制面流程：

1. 初始化运行环境
2. 创建通信域
3. 分配内存资源
4. 注册对称window
5. 创建通信组team
6. 下发team/window句柄到Kernel
7. 销毁通信资源

数据面流程：

1. 预处理
2. 前同步
3. 发起通信任务
4. 后同步

## 控制面资源准备

### 初始化运行环境

  ```cpp
  uint32_t rank;   // 当前rankId
  uint32_t nranks; // rank总数
  aclInit(nullptr);
  aclrtSetDevice(rank);
  aclrtStream stream = nullptr;
  aclrtCreateStream(&stream);
  ```

### 创建通信域

通信域内所有rank必须共用同一份rootInfo建域：由root节点（示例中取rank 0）调用[`HcclGetRootInfo`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclGetRootInfo.md)生成rootInfo，并通过rank间信息交换将同一份rootInfo分发给所有rank，各rank均拿到rootInfo后再共同调用[`HcclCommInitRootInfo`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCommInitRootInfo.md)创建通信域。

样例工程通过以rank 0为中心的TCP socket控制通道交换rootInfo，参见[`examples/aicore/utils/rank_sync.h`](../../../examples/aicore/utils/rank_sync.h)（控制通道）与[`examples/aicore/utils/hccl_comm_init.h`](../../../examples/aicore/utils/hccl_comm_init.h)（rootInfo交换与建域）。

  ```cpp
  // 控制通道endpoint（由命令行传入），形如"tcp://ip:port"
  std::string endpoint;
  // 建立TCP控制通道：rank 0监听endpoint，其余rank连接
  examples::RankSyncContext sync = (rank == 0) ?
      examples::RankSyncContext::Listen(endpoint, nranks) :
      examples::RankSyncContext::Connect(rank, nranks, endpoint);
  // 生成root节点的rank标识信息：由root节点生成，通信域内所有rank必须共用同一份rootInfo
  HcclRootInfo rootInfo;
  if (rank == 0) {
      HcclGetRootInfo(&rootInfo);
  }
  // 通过控制通道AllGather交换rootInfo，所有rank取用rank 0的rootInfo
  std::vector<HcclRootInfo> allRootInfo(nranks);
  sync.Allgather(examples::kTagRootInfo, &rootInfo, sizeof(rootInfo), allRootInfo.data());
  // 所有rank使用同一份rootInfo初始化通信域
  HcclComm hcclComm;
  HcclCommInitRootInfo(nranks, &allRootInfo[0], rank, &hcclComm);
  ```

也可采用文件交换等其他方式：由root节点将rootInfo写入共享路径下的文件，其他rank等待文件就绪后读取同一份rootInfo。

### 分配内存资源

分配内存：每个rank分别通过`HcommMemAlloc`分配发送缓冲区、接收缓冲区和同步信号缓冲区。参见[`HcommMemAlloc`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcommMemAlloc.md)。

AllGather场景下，单个分片大小为一个`uint32_t`，接收缓冲区需容纳`nranks`个分片。

  ```cpp
  // 缓冲区大小常量
  constexpr uint64_t SEND_BUF_SIZE = sizeof(uint32_t);   // 单个分片大小
  const uint64_t RECV_BUF_SIZE = SEND_BUF_SIZE * nranks; // 容纳nranks个分片
  constexpr uint64_t SIGNAL_BUF_SIZE = sizeof(uint64_t); // 同步信号宽度

  void* sendBuf = nullptr;
  void* recvBuf = nullptr;
  void* signalBuf = nullptr;
  HcommMemAlloc(&sendBuf, SEND_BUF_SIZE);
  HcommMemAlloc(&recvBuf, RECV_BUF_SIZE);
  HcommMemAlloc(&signalBuf, SIGNAL_BUF_SIZE);
  ```

分配完成后初始化本端数据。

  ```cpp
  // 初始化本端输入：每个分片存储一个uint32_t数值，即本端rank编号
  uint32_t sendVal = rank;
  aclrtMemcpy(sendBuf, SEND_BUF_SIZE, &sendVal, sizeof(sendVal), ACL_MEMCPY_HOST_TO_DEVICE);

  // 本端分片自填充：将sendBuf拷贝到本端recvBuf的rankId分片
  aclrtMemcpy(static_cast<uint8_t*>(recvBuf) + static_cast<uint64_t>(rank) * SEND_BUF_SIZE, SEND_BUF_SIZE,
      sendBuf, SEND_BUF_SIZE, ACL_MEMCPY_DEVICE_TO_DEVICE);
  ```

### 注册对称window

将本端的发送缓冲区、接收缓冲区和同步信号缓冲区分别注册为对称window；各rank需按相同的内存地址和大小注册对应window，以实现对称访问。参见[`HcclCommSymWinRegister`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCommSymWinRegister.md)。

  ```cpp
  HcclCommSymWindow sendWin{nullptr};
  HcclCommSymWinRegister(hcclComm, sendBuf, SEND_BUF_SIZE, &sendWin, 1);

  HcclCommSymWindow recvWin{nullptr};
  HcclCommSymWinRegister(hcclComm, recvBuf, RECV_BUF_SIZE, &recvWin, 1);

  HcclCommSymWindow signalWin{nullptr};
  HcclCommSymWinRegister(hcclComm, signalBuf, SIGNAL_BUF_SIZE, &signalWin, 1);
  ```

### 创建通信组team

配置team描述并创建team。参见[`HcclTeamCreateDescInit`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclTeamCreateDescInit.md)和[`HcclTeamCreate`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclTeamCreate.md)。

  ```cpp
  HcclTeamCreateDesc desc;
  HcclTeamCreateDescInit(&desc);
  std::vector<uint32_t> worldRankIds;
  for (uint32_t idx = 0; idx < nranks; idx++) {
      worldRankIds.emplace_back(idx);
  }
  desc.rankIds = worldRankIds.data();
  desc.rankNum = nranks;
  desc.selfRankId = rank;
  desc.netLayer = 0;
  desc.requirement.barrierCount = 1;
  desc.engine = COMM_ENGINE_AIV;
  desc.protocol = COMM_PROTOCOL_UB_CTP;
  desc.channelCnt = 1;
  HcommTeamHandle team{nullptr};
  HcclTeamCreate(hcclComm, &desc, &team);
  ```

### 下发句柄到Kernel

将`team`、`sendWin`、`recvWin`、`signalWin`等device侧句柄作为Kernel入参下发。

  ```cpp
  all_gather_kernel<<<1U, nullptr, stream>>>(team, sendWin, recvWin, signalWin, nranks, rank);
  aclrtSynchronizeStream(stream);
  ```

### 销毁通信资源

通信任务完成后，销毁通信资源。销毁顺序与创建顺序相反。

  ```cpp
  // 销毁team
  HcclTeamDestroy(team);
  // 注销window
  HcclCommSymWinDeregister(recvWin);
  HcclCommSymWinDeregister(sendWin);
  // 释放申请的内存
  HcommMemFree(sendBuf);
  HcommMemFree(recvBuf);
  HcommMemFree(signalBuf);
  // 销毁通信域
  HcclCommDestroy(hcclComm);
  // 去初始化
  aclrtDestroyStream(stream);
  aclrtResetDevice(static_cast<uint32_t>(rank));
  aclFinalize();
  ```

## Kernel侧通信执行

Kernel入口函数如下，team与对称window句柄以指针形式传入，后续步骤在该函数内展开。

  ```cpp
  extern "C" __global__ __aicore__ void all_gather_kernel(
      __gm__ void* team, __gm__ void* sendWin, __gm__ void* recvWin, __gm__ void* signalWin,
      uint32_t rankNum, uint32_t rankId)
  {
      // 后续Kernel侧通信执行任务在该函数内展开
  }
  ```

### 预处理

通信任务下发前的准备如下。

  ```cpp
  // 准备UBuf工作区：Put/Get/PutValue/Signal接口需要提供不小于512B的UBuf临时工作区
  AscendC::TPipe pipe;
  AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
  pipe.InitBuffer(hcommBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
  AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
  AscendC::AinDescriptorUbuf hcommUbuf{
      reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
      AscendC::HCOMM_URMA_TMP_BUF_SIZE, 0U};
  // 创建Ain和AinBarrierSession对象
  AscendC::Ain ain(0);
  AscendC::AinBarrierSession ainBarrier(&ain, team, 0);
  // 读取同步信号signal初始值
  uint64_t signalVal = ain.ReadSignal(team, signalWin, 0, 32);
  ```

### 前同步

前同步：保证所有rank都完成预处理，读取的signal初始值是未被远端通信污染的，预处理就绪后再执行后续通信。

  ```cpp
  ainBarrier.Sync(AscendC::AinMemoryOrder::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
  ```

### 发起通信任务

AllGather：向其他rank提交`Put`通信任务，并向对端发送`Signal`同步信号。

  ```cpp
  for (uint32_t peer = 0; peer < rankNum; peer++) {
      if (peer == rankId) {
          continue;
      }
      ain.Put(
          team, peer, recvWin, rankId * SEND_BUF_SIZE, sendWin, 0U, SEND_BUF_SIZE,
          AscendC::AinSignalInc{signalWin, 0}, hcommUbuf);
  }
  ```

### 后同步

阻塞直至所有rank都接收到来自其他rank的数据，AllGather完成。

  ```cpp
  // 本端同步：等待Put通信任务完成
  ain.Flush(team);
  // 远端点对点同步：阻塞直至收到其他所有rank发来的数据
  ain.WaitSignal(team, signalWin, 0, signalVal + rankNum - 1, 32);
  // 屏障同步：阻塞直至所有rank都接收到来自其他rank的数据
  ainBarrier.Sync(AscendC::AinMemoryOrder::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
  ```

## 延迟提交与批量优化

默认提交方式为`AIN_COMMIT_IMMED`，每次调用立即提交。在需要提交多个通信任务时，可使用`AIN_COMMIT_DELAYED`延迟提交，只在最后一次提交任务设置`AIN_COMMIT_IMMED`实现批量提交，以此减少开销。

  ```cpp
  // 延迟提交Config
  static constexpr struct AscendC::UrmaWqeEntry COMMIT_DELAYED_CONFIG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0, // 延迟提交场景不产生cqe
    .inlineEn = 0,
  };
  // 最后一次提交Config
  static constexpr struct AscendC::UrmaWqeEntry COMMIT_IMMED_CONFIG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1, // 最后一次提交产生cqe
    .inlineEn = 0,
  };
  // 延迟提交：只组装WQE，不提交
  ain.Put<AscendC::AinRemoteNone, AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED, COMMIT_DELAYED_CONFIG>(
      team, peer, recvWin, dstOffset,
      sendWin, 0U, dataLen, AscendC::AinRemoteNone{}, hcommUbuf);
  // ... 更多延迟提交任务
  // 最后一次使用AIN_COMMIT_IMMED统一触发提交
  ain.Put<AscendC::AinRemoteNone, AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_IMMED, COMMIT_IMMED_CONFIG>(
      team, peer, recvWin, dstOffset2,
      sendWin, 0U, dataLen2, AscendC::AinRemoteNone{}, hcommUbuf);
  ain.Flush(team);
  ```

约束：

- 使用`AIN_COMMIT_DELAYED`时，连续调用次数不得超过SQ（Send Queue，发送队列）深度（`sqDepth`）。
- 批量提交场景下，仅最后一次`AIN_COMMIT_IMMED`应产生CQE（Completion Queue Element，完成队列元素）。

## 异步等待

`Flush`会阻塞等待team内所有peer的通信通道任务完成；如果只需要等待特定peer的通信通道任务，使用`FlushAsync`+`Wait`实现异步等待，避免阻塞当前Kernel执行。

  ```cpp
  // 获取指定peer的通道句柄
  AscendC::ChannelHandle handle;
  ain.FlushAsync(team, peer, &handle);
  // ... 执行其他计算
  // 等待该peer的通信通道任务完成
  ain.Wait(handle);
  ```

## 注意事项

- `Put`/`Get`的单次数据长度范围为`0 < bytes <= 256MB`。
- `Ain`当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。
- `AinDescriptorUbuf`需要提供不小于512B的UBuf（Unified Buffer）临时工作区。
- 对称window需要在所有rank上注册相同内存地址和大小，`Put`/`Get`/`PutValue`/`Signal`通过offset定位数据位置。
- `Signal`地址应按`uint64_t`对齐访问要求准备。
