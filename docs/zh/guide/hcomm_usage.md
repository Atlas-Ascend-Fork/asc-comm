# Hcomm通信接口使用说明

## 概述

Hcomm是asc-comm当前提供的AICore侧点对点通信接口。使用方通过`AscendC::Hcomm`模板选择通信协议。普通接口通过`ChannelHandle`逐条提交通信任务；Ascend 950的UBC_CTP/URMA路径还支持通过BatchHandle添加多个任务并统一提交。

## 普通接口流程

1. 包含头文件。

   ```cpp
   #include "hcomm/hcomm.h"
   ```

2. 创建Hcomm对象。

   ```cpp
   AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
   ```

3. 调用`Init`初始化普通接口使用的临时工作区。

   ```cpp
   int32_t ret = hcomm.Init(tmpBuf, tmpLen);
   ```

4. 提交通信任务。

   ```cpp
   ret = hcomm.WriteNbi(channel, dst, src, len);
   ret = hcomm.ReadNbi(channel, dst, src, len);
   ret = hcomm.WriteWithNotifyNbi(channel, dst, src, len, notifyAddr,    notifyVal);
   ret = hcomm.AtomicFAA<uint64_t>(channel, remoteCounter, fetchAddr,    addVal);
   ret = hcomm.AtomicCAS<uint64_t>(channel, remoteValue, fetchAddr,    compareVal, swapVal);
   ```

5. 如果提交任务时设置`commit = false`，显式调用`Commit`。

   ```cpp
   ret = hcomm.WriteNbi<false>(channel, dst, src, len);
   ret = hcomm.Commit(channel);
   ```

6. 调用普通`Drain`等待任务完成。

   ```cpp
   ret = hcomm.Drain(channel);
   ```

## 单通道批量接口流程

批量接口当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不需要调用`Init`。

1. 准备UB工作区，并从`ChannelHandle`创建批量句柄。建议使用`auto`接收返回值。

   ```cpp
   AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
   AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>();
   auto batchHandle = hcomm.MakeBatchHandle(
       channel, batchBuffer, batchBufferLen, remoteAddr);
   ```

   `remoteAddr`用于选择本批任务访问的远端注册内存。传入`nullptr`时选择第一个远端注册buffer。

2. 向当前批次添加读、写或写通知任务。同一批次可以混合使用三种接口。

   ```cpp
   int32_t ret = hcomm.WriteNbi(
       batchHandle, remoteWriteDst, localWriteSrc, writeLen);
   ret = hcomm.ReadNbi(
       batchHandle, localReadDst, remoteReadSrc, readLen);
   ret = hcomm.WriteWithNotifyNbi(
       batchHandle, remoteNotifyDst, localNotifySrc,
       notifyLen, notifyAddr, notifyVal);
   ```

3. 提交当前批次。

   ```cpp
   ret = hcomm.BatchCommit(batchHandle);
   ```

   提交成功后，可以复用批量句柄和工作区继续添加并提交下一批任务。

4. 等待通过该批量句柄提交的任务所生成的完成记录。

   ```cpp
   ret = hcomm.Drain(batchHandle);
   ```

## 多通道批量接口流程

使用HCCL通信域时，Host侧调用`MakeMultiChannelHandle`的通信域重载创建`MultiChannelHandle`：

```cpp
#include "hcomm/hcomm_host.h"

MultiChannelHandle multiChannel = 0U;
HcclResult ret = MakeMultiChannelHandle(
    comm, sharedQueueTag, channelDescs, channelNum, &multiChannel);
```

该方式创建的资源由HCCL通信域管理，并随通信域销毁自动释放。

不使用HCCL通信域时，传入Hcomm Endpoint和`HcommChannelDesc`调用另一个重载。Kernel执行并同步完成后，需要先销毁多通道句柄，再销毁Endpoint：

```cpp
HcommResult ret = MakeMultiChannelHandle(
    endpointHandle, channelDescs, channelNum, &multiChannel);

// Launch and synchronize the Kernel before destroying the handle.
ret = DestroyMultiChannelHandle(multiChannel);
ret = HcommEndpointDestroy(endpointHandle);
```

AICore侧通过`GetHandleRef`选择逻辑通道和远端注册内存。使用该接口返回的句柄引用添加任务，使用`MakeBatchHandle`返回的多通道批量句柄提交和等待：

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
auto multiBatchHandle = hcomm.MakeBatchHandle(
    static_cast<AscendC::MultiChannelHandle>(multiChannel),
    batchBuffer, batchBufferLen);

for (uint32_t channelIndex = 0U; channelIndex < channelNum; ++channelIndex) {
    auto& peerBatchHandle = hcomm.GetHandleRef(
        multiBatchHandle, channelIndex, remoteBuffers[channelIndex]);
    ret = hcomm.WriteNbi(
        peerBatchHandle, remoteBuffers[channelIndex], localBuffer, dataLen);
}

ret = hcomm.BatchCommit(multiBatchHandle);
ret = hcomm.Drain(multiBatchHandle);
```

`channelIndex`对应创建`MultiChannelHandle`时`channelDescs`中的数组下标。切换逻辑通道或远端注册buffer时，需要重新调用`GetHandleRef`。对同一个多通道批量句柄重复调用时，返回的引用指向同一个内层BatchHandle，并随最近一次调用更新其逻辑通道和远端注册内存选择。

## 批量接口约束

- 仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`。
- 批量流程不需要调用`Init`，其UB工作区由`MakeBatchHandle`绑定。
- 工作区起始地址必须按32字节对齐；`batchBufferLen`至少为64字节，且不能超过`batchBuffer`的实际容量。
- 每个批量`ReadNbi`或`WriteNbi`任务占用64字节工作区，每个批量`WriteWithNotifyNbi`任务占用128字节。单批次任务占用的工作区总量不能超过`batchBufferLen`，占用的64字节任务槽位数量还必须小于Host侧创建通道资源时确定的任务提交容量，否则`BatchCommit`返回`-1`。
- 批量接口要求`config.inlineEn = 0`，`config.cqe`只能为`0`或`1`。
- 默认`URMA_DEFAULT_CFG`的`cqe`为`1`，因此每个任务都会生成完成记录。若所有已提交任务均设置为`cqe = 0`，批量`Drain`会直接返回，但不能用于确认任务已经完成。
- 在顺序和fence配置能够保证最后一个任务在之前任务之后完成时，可以只为最后一个任务设置`cqe = 1`，再通过批量`Drain`确认整批任务完成。
- 批量`WriteNbi`和`WriteWithNotifyNbi`的远端目的区间、批量`ReadNbi`的远端源区间，必须属于创建或选择BatchHandle时指定的远端注册内存。
- 批量`WriteWithNotifyNbi`的远端目的区间和`notifyAddr`必须属于同一远端注册内存。
- 调用`BatchCommit`前，当前批次必须至少包含一个任务。调用批量`Drain`前，当前批次中的任务必须已全部提交。
- 可以连续调用多次`BatchCommit`后统一调用一次批量`Drain`，但累计未完成任务数量和完成记录数量不能超过通道配置的相应容量。
- 使用BatchHandle期间必须独占其关联的通道资源，不能交叉调用普通接口，也不能并发使用其他BatchHandle。
- BatchHandle、通道资源、`MultiChannelHandle`和批量工作区的生命周期由调用方负责。所有批量操作完成前不得释放或并发复用这些资源。

## 协议说明

| 协议 | 说明 |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | 支持普通`ReadNbi`、`WriteNbi`、`Commit`和`Drain`，不支持BatchHandle接口。 |
| `COMM_PROTOCOL_UBC_CTP` | 支持普通读、写、写通知、原子、提交和等待接口；Ascend 950平台的AIV还支持`Lock`、`Unlock`及BatchHandle批量接口。 |

## 多AI Core共享通道

Ascend 950平台上，多个AIV共享同一个`COMM_PROTOCOL_UBC_CTP`通道时，需要通过[Lock](../api/aicore/hcomm/Lock.md)和[Unlock](../api/aicore/hcomm/Unlock.md)保护会更新通道状态的访问。每个AI Core可以使用独立的Hcomm对象和UB临时工作区，但传入相同的`ChannelHandle`。

```cpp
int32_t lockRet = hcomm.Lock(channel);
if (lockRet == 0) {
    int32_t opRet = hcomm.WriteNbi(channel, dst, src, len);

    // 即使通信接口返回失败，也必须释放已经成功获取的锁。
    int32_t unlockRet = hcomm.Unlock(channel);
    // 分别处理opRet和unlockRet。
}
```

锁不可重入。每次成功调用`Lock`后都必须由同一AI Core调用`Unlock`，错误退出路径也需要释放锁。接口仅同步同一设备上共享同一通道的AI Core。

## 注意事项

- 普通接口需要先调用`Init`提供临时工作区，当前最小工作区大小为512字节。
- 使用`__ubuf__ uint8_t*`调用`Init`时，实现会对临时工作区起始地址做32字节对齐；使用`LocalTensor`时，调用方需要保证tensor容量满足工作区要求。
- `ChannelHandle`指向的通道实体由调用方负责初始化和维护。
- `WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`普通接口仅支持`COMM_PROTOCOL_UBC_CTP`路径。
- 原子操作的数据类型仅支持`int32_t`、`uint32_t`、`int64_t`和`uint64_t`。

## 样例

可参考[hcomm_write_read_nbi](../../../examples/aicore/hcomm/01_hcomm_write_read_nbi/README.md)了解普通接口的AIV Kernel侧调用方式和Host侧通信资源创建流程；参考[hcomm_batch_write](../../../examples/aicore/hcomm/02_hcomm_batch_write/README.md)了解多通道批量写流程。

两个样例均依赖Ascend 950PR/Ascend 950DT和至少2张NPU完成运行验证；单卡环境仅支持编译验证。
