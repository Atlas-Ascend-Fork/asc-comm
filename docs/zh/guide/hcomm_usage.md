# Hcomm通信接口使用说明

## 概述

Hcomm是asc-comm当前提供的AICore侧点对点通信接口。使用方通过`AscendC::Hcomm`模板选择通信协议。普通接口通过`ChannelHandle`逐条提交通信任务；Ascend 950的UBC_CTP/URMA路径还支持BatchHandle，先在UB中批量准备WQE，再一次复制到GM SQ并提交。

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

## 单通道BatchHandle接口流程

BatchHandle接口当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不依赖`Init`。

1. 准备UB缓冲区，并从`ChannelHandle`创建批量句柄。调用侧建议使用`auto`接收返回值。

   ```cpp
   AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
   AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>   ();
   auto batchHandle = hcomm.MakeBatchHandle(
       channel, batchBuffer, batchBufferLen, remoteAddr, localAddr);
   ```

   `remoteAddr`非空时需要落在通道的远端已注册buffer中；为空时，接口选择远端注册表中的第0个buffer。这是唯一一次远端注册区查找：接口缓存选中buffer的`tokenId/tokenValue`，后续批量接口不再校验远端地址范围；`localAddr`在当前版本中为预留参数。

2. 在BatchHandle的UB缓冲区中准备WQE。读、写和写通知任务可以在同一批次中混合。

   ```cpp
   int32_t ret = hcomm.WriteNbi(batchHandle, remoteWriteDst,    localWriteSrc, writeLen);
   ret = hcomm.ReadNbi(batchHandle, localReadDst, remoteReadSrc,    readLen);
   ret = hcomm.WriteWithNotifyNbi(
       batchHandle, remoteNotifyDst, localNotifySrc, notifyLen,    notifyAddr, notifyVal);
   ```

   这些调用只准备WQE，不复制到GM SQ，也不敲doorbell。调用方必须保证批量Write/WriteWithNotify的远端目的区间、批量Read的远端源区间以及`notifyAddr`，都属于创建句柄时缓存的`tokenId/tokenValue`所代表的注册内存。

3. 调用`BatchCommit`复制并提交当前批次。该接口成功后始终敲SQ doorbell。

   ```cpp
   ret = hcomm.BatchCommit(batchHandle);
   ```
   
   提交成功后，BatchHandle和UB缓冲区可以继续准备下一个批次。可以执行多次   `BatchCommit`，但调用方必须保证累计未完成任务不会覆盖SQ中尚未消费的WQE，   且自上次批量`Drain`以来累计生成但尚未消费的CQE不能超过CQ容量。提交阶段不   会自动轮询CQ。

4. 调用批量`Drain`等待已提交任务。

   ```cpp
   ret = hcomm.Drain(batchHandle);
   ```
   
   批量`Drain`复用BatchHandle WQE缓冲区的第一个64字节作为CQE临时空间，因此   不需要`Init`。调用前BatchHandle中不能存在尚未`BatchCommit`的WQE。

## 共享Jetty BatchHandle接口流程

使用HCCL通信域时，Host侧调用`MakeMultiChannelHandle`的通信域重载创建共享Jetty通道和`MultiChannelHandle`：

```cpp
#include "hcomm/hcomm_host.h"

MultiChannelHandle multiChannel = 0U;
HcclResult ret = MakeMultiChannelHandle(
    comm, sharedQueueTag, channelDescs, channelNum, &multiChannel);
// 共享通道和Device上下文由通信域管理，并在通信域销毁时自动释放。
```

不使用HCCL通信域时，传入Hcomm Endpoint和`HcommChannelDesc`调用`MakeMultiChannelHandle`。该路径的资源不由通信域管理，使用结束后必须先销毁多通道句柄，再销毁Endpoint：

```cpp
HcommResult ret = MakeMultiChannelHandle(endpointHandle, channelDescs, channelNum, &multiChannel);
// Launch and synchronize the Kernel before destroying the handle.
ret = DestroyMultiChannelHandle(multiChannel);
ret = HcommEndpointDestroy(endpointHandle);
```

AICore侧创建多通道批量句柄，通过`GetHandleRef`取得统一的内层执行句柄。内层句柄用于准备WQE，外层多通道批量句柄用于`BatchCommit`和`Drain`：

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
auto multiBatchHandle = hcomm.MakeBatchHandle(
    static_cast<AscendC::MultiChannelHandle>(multiChannel), batchBuffer, batchBufferLen);
for (uint32_t channelIndex = 0U; channelIndex < channelNum; ++channelIndex) {
    auto& peerBatchHandle = hcomm.GetHandleRef(
        multiBatchHandle, channelIndex, remoteBuffers[channelIndex]);
    ret = hcomm.WriteNbi(
        peerBatchHandle, remoteBuffers[channelIndex], localBuffer, dataLen);
}
ret = hcomm.BatchCommit(multiBatchHandle);
ret = hcomm.Drain(multiBatchHandle);
```

`channelDescs`数组下标对应`GetHandleRef`的`channelIndex`。`remoteAddr`用于在该逻辑通道的远端MR表中选择并缓存token；为空时选择第一个远端MR。批量读写使用该接口返回的内层引用；`BatchCommit`和`Drain`使用外层多通道批量句柄。

## 批量缓冲区和CQE配置

- UBC_CTP的每个WQEBB为64字节。批量`ReadNbi`和`WriteNbi`各占1个WQEBB，批量`WriteWithNotifyNbi`占2个WQEBB。
- 缓冲区WQEBB容量为`batchBufferLen / 64`。单批次所需容量为“读任务数 + 写任务数 + 2 × 写通知任务数”。
- `batchBufferLen`至少为64字节，并且必须严格小于`sqDepth * 64`字节。
- 批量接口要求`config.inlineEn = 0`，`config.cqe`支持`0`或`1`，同一批次可以混用两种CQE配置。
- 默认`URMA_DEFAULT_CFG`会为每个请求生成CQE。若所有已提交请求均使用`cqe = 0`，批量`Drain`没有可轮询的CQE，会直接返回，不能据此确认硬件已经完成这些请求。
- 在顺序和fence配置能够保证最后一个请求在前序请求之后完成时，可以只为最后一条请求设置`cqe = 1`，然后统一调用一次批量`Drain`。

## 协议说明

| 协议 | 说明 |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | 支持普通`ReadNbi`、`WriteNbi`、`Commit`和`Drain`，不支持BatchHandle接口。 |
| `COMM_PROTOCOL_UBC_CTP` | 支持普通读、写、写通知、原子、提交和等待接口；Ascend 950平台的AIV还支持`Lock`、`Unlock`、批量读、批量写、批量写通知、`BatchCommit`和批量`Drain`。 |

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

- 普通接口需要先调用`Init`提供临时工作区，当前最小工作区大小为512字节。BatchHandle接口不需要调用`Init`。
- 使用`__ubuf__ uint8_t*`调用`Init`时，实现会对临时工作区起始地址做32字节对齐；使用`LocalTensor`时，调用方需要保证tensor容量满足工作区要求。
- `MakeBatchHandle`使用的UB缓冲区起始地址必须按32字节对齐，其容量和生命周期由调用方负责。
- `ChannelHandle`指向的通道实体由调用方负责初始化和维护。
- BatchHandle缓存创建时的SQ/CQ上下文和队列游标。使用BatchHandle期间，调用方必须独占对应单通道或共享Jetty，不能交叉调用普通接口，也不能并发使用多个BatchHandle。
- 批量接口的单次数据长度不能大于`UINT32_MAX`。
- `WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`普通接口仅支持`COMM_PROTOCOL_UBC_CTP`路径。
- 原子操作的数据类型仅支持`int32_t`、`uint32_t`、`int64_t`和`uint64_t`。

## 样例

可参考[hcomm_write_read_nbi](../../../examples/hcomm_write_read_nbi/README.md)了解普通接口的AIV Kernel侧调用方式和Host侧通信资源创建流程；参考[hcomm_batch_write](../../../examples/hcomm_batch_write/README.md)了解共享Jetty批量写流程。

该样例在两卡场景下对称执行普通`WriteNbi`和`ReadNbi`：

```cpp
hcomm.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);
hcomm.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);
hcomm.Drain(channel);
```

两个样例均依赖Ascend 950PR/Ascend 950DT和至少2张NPU完成运行验证；单卡环境仅支持编译验证。
