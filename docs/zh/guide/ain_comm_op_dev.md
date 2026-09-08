# 基于Ain的通信算子开发指南

## 概述

Ain（Ascend-Initiated Networking）是AICore侧单边通信接口模板，提供`Put`/`Get`/`PutValue`/`Signal`等单边通信能力及`Flush`/`WaitSignal`/`Sync`同步能力。开发者无需在Kernel侧直接维护远端物理地址，Ain会根据通信team、对端rank和对称window句柄自动解析通信通道与通信内存地址。

本指南介绍Ain通信算子的完整开发流程。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 通信协议 | `COMM_PROTOCOL_UB_CTP` |
| 通信引擎 | `COMM_ENGINE_AIV` |

> [!NOTE]说明
<!-- npu="950" id1 -->
> 仅Ascend 950PR / Ascend 950DT支持实现Ain算子。单卡环境仅支持编译验证，实际运行需要至少2张NPU。
<!-- end id1 -->

## 总体流程

Ain通信算子开发分为控制面资源准备和数据面通信执行两阶段。

控制面流程：

1. 初始化ACL运行环境
2. 创建HCCL通信域
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

### 初始化ACL运行环境

  ```cpp
  uint32_t rank;   // 当前rankId
  uint32_t nranks; // rank总数
  ACL_CHECK(aclInit(nullptr));
  ACL_CHECK(aclrtSetDevice(static_cast<uint32_t>(rank)));
  aclrtStream stream = nullptr;
  ACL_CHECK(aclrtCreateStream(&stream));
  ```

### 创建HCCL通信域

创建HcclComm通信域：参见[`HcclCommInitClusterInfo`](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/920beta1/commlib/hcclug/docs/zh/api_ref/comm_mgr_c/HcclCommInitClusterInfo.md)或[`HcclCommInitRootInfo`](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/920beta1/commlib/hcclug/docs/zh/api_ref/comm_mgr_c/HcclCommInitRootInfo.md)。

- 基于rank table创建通信域

  ```cpp
  // rank table配置文件路径
  char *rankTableFile = "/path/rank_table.json";
  // 创建通信域
  HcclComm hcclComm;
  // 此样例以devId作为当前rank的rank id
  HcclCommInitClusterInfo(rankTableFile, rank, &hcclComm);
  ```

- 基于rootInfo创建通信域

  ```cpp
  // 生成root节点的rank标识信息
  HcclRootInfo rootInfo;
  HcclGetRootInfo(&rootInfo);
  // 初始化通信域
  HcclComm hcclComm;
  HcclCommInitRootInfo(nranks, &rootInfo, rank, &hcclComm);
  ```

### 分配内存资源

分配内存：每个rank分别分配发送缓冲区、接收缓冲区和同步信号缓冲区。缓冲区大小需满足通信语义要求，例如AllGather场景下接收缓冲区需不小于`nranks * SEND_BUF_SIZE`。

  ```cpp
  void* sendBuf = nullptr;
  void* recvBuf = nullptr;
  void* signalBuf = nullptr;
  aclrtMalloc(&sendBuf, SEND_BUF_SIZE, ACL_MEM_MALLOC_HUGE_FIRST);
  aclrtMalloc(&recvBuf, RECV_BUF_SIZE, ACL_MEM_MALLOC_HUGE_FIRST);
  aclrtMalloc(&signalBuf, SIGNAL_BUF_SIZE, ACL_MEM_MALLOC_HUGE_FIRST);
  ```

### 注册对称window

将本端的发送缓冲区、接收缓冲区和同步信号缓冲区分别注册为对称window；各rank需按相同的内存地址和大小注册对应window，以实现对称访问。参见[`HcclCommSymWinRegister`](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/920beta1/commlib/hcclug/docs/zh/api_ref/comm_mgr_c/HcclCommSymWinRegister.md)。

  ```cpp
  HcclCommSymWindow sendWin{nullptr};
  HCCL_CHECK(HcclCommSymWinRegister(hcclComm, sendBuf, SEND_BUF_SIZE, &sendWin, 1));

  HcclCommSymWindow recvWin{nullptr};
  HCCL_CHECK(HcclCommSymWinRegister(hcclComm, recvBuf, RECV_BUF_SIZE, &recvWin, 1));

  HcclCommSymWindow signalWin{nullptr};
  HCCL_CHECK(HcclCommSymWinRegister(hcclComm, signalBuf, SIGNAL_BUF_SIZE, &signalWin, 1));
  ```

### 创建通信组team

配置team描述并创建team。

  ```cpp
  HcclTeamCreateDesc desc;
  HCCL_CHECK(HcclTeamCreateDescInit(&desc));
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
  HCCL_CHECK(HcclTeamCreate(hcclComm, &desc, &team));
  ```

### 下发句柄到Kernel

将`team`、`sendWin`、`recvWin`、`signalWin`等device侧句柄作为Kernel入参下发。

  ```cpp
  all_gather_kernel<<<1U, nullptr, stream>>>(team, sendWin, recvWin, signalWin, nranks, rank);
  ACL_CHECK(aclrtSynchronizeStream(stream));
  ```

### 销毁通信资源

通信任务完成后，销毁通信资源。销毁顺序与创建顺序相反。

  ```cpp
  // 销毁team
  HCCL_CHECK(HcclTeamDestroy(team));
  // 注销window
  HCCL_CHECK(HcclCommSymWinDeregister(recvWin));
  HCCL_CHECK(HcclCommSymWinDeregister(sendWin));
  // 释放申请的内存
  ACL_CHECK(aclrtFree(sendBuf));
  ACL_CHECK(aclrtFree(recvBuf));
  ACL_CHECK(aclrtFree(signalBuf));
  // 销毁通信域
  HCCL_CHECK(HcclCommDestroy(hcclComm));
  // 去初始化
  ACL_CHECK(aclrtDestroyStream(stream));
  ACL_CHECK(aclrtResetDevice(static_cast<uint32_t>(rank)));
  ACL_CHECK(aclFinalize());
  ```

## Kernel侧通信执行

以AllGather为例说明Kernel侧开发流程。Kernel入口函数如下，team与对称window句柄以指针形式传入，后续步骤在该函数内展开：

  ```cpp
  extern "C" __global__ __aicore__ void all_gather_kernel(
      __gm__ void* team, __gm__ void* sendWin, __gm__ void* recvWin, __gm__ void* signalWin,
      uint32_t rankNum, uint32_t rankId)
  {
      // 下文步骤1~4在该函数内展开
  }
  ```

### 预处理

准备UBuf工作区：`Ain`的`Put`/`Get`/`Signal`等接口需要提供不小于512字节的UBuf临时工作区。

创建Ain和AinBarrierSession对象。

读取同步信号signal初始值。

  ```cpp
  AscendC::TPipe pipe;
  AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
  pipe.InitBuffer(hcommBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
  AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
  AscendC::AinDescriptorUbuf hcommUbuf{
      reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
      AscendC::HCOMM_URMA_TMP_BUF_SIZE, 0U};

  AscendC::Ain ain(0);
  AscendC::AinBarrierSession ainBarrier(&ain, team, 0);

  uint32_t signalVal = ain.ReadSignal(team, signalWin, 0, 32);
  ```

### 前同步

前同步：保证所有rank都完成预处理，读取的signal初始值是未被远端通信污染的，预处理就绪后再执行后续通信。

  ```cpp
  ainBarrier.Sync(AscendC::AinMemoryOrder::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
  ```

### 发起通信任务

AllGather：向其他rank提交Put通信任务，并向对端发送Signal同步信号。

  ```cpp
  for (uint32_t peer = 0; peer < rankNum; peer++) {
      if (peer == rankId) {
          continue;
      }
      ain.Put(
          team, peer, recvWin, rankId * SEND_BYTES, sendWin, 0U, SEND_BYTES,
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

默认提交方式为`AIN_COMMIT_IMMED`，每次调用立即提交。在需要提交多个通信任务时，可使用`AIN_COMMIT_DELAYED`延迟提交，只在最后一次提交任务设置`AIN_COMMIT_IMMED`实现批量提交，以此减少开销：

  ```cpp
  // 延迟提交：只组装WQE，不提交
  ain.Put<AinRemoteNone, AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED>(
      team, peer, recvWin, dstOffset,
      sendWin, 0U, dataLen, AscendC::AinRemoteNone{}, hcommUbuf);
  // ... 更多延迟提交任务
  // 最后一次使用AIN_COMMIT_IMMED统一触发提交
  ain.Put<AinRemoteNone, AinDescriptorUbuf, AscendC::AIN_COMMIT_IMMED>(
      team, peer, recvWin, dstOffset2,
      sendWin, 0U, dataLen2, AscendC::AinRemoteNone{}, hcommUbuf);
  ain.Flush(team);
  ```

约束：

- 使用`AIN_COMMIT_DELAYED`时，连续调用次数不得超过SQ深度（`sqDepth`）。
- 批量提交场景下，仅最后一次`AIN_COMMIT_IMMED`应产生CQE。

## 异步等待

除`Flush`外，还可使用`FlushAsync`+`Wait`实现异步等待，避免阻塞当前Kernel执行：

  ```cpp
  // 获取指定peer的通道句柄
  auto handle = ain.FlushAsync(team, peer);
  // ... 执行其他计算
  // 等待该peer通道任务完成
  ain.Wait(handle);
  ```

## 注意事项

- `Put`/`Get`的单次数据长度范围为`0 < bytes <= 256MB`。
- `Ain`当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。
- `ubuf`需要提供不小于512B的UBuf临时工作区。
- `Flush`会阻塞等待team内所有peer通道任务完成；如果只需要等待特定peer，使用`FlushAsync`+`Wait`。
- 对称window需要在所有rank上注册相同内存地址和大小，`Put`/`Get`通过offset定位数据位置。
- `Signal`地址应按`uint64_t`对齐访问要求准备。
