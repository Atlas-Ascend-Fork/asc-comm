# Hcomm

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径：`"hcomm/hcomm_simt.h"`。

`AscendC::simt::Hcomm`是SIMD（Single Instruction Multiple Data，单指令多数据）路径`AscendC::Hcomm`在SIMT（Single Instruction Multiple Threads，单指令多线程）执行空间的对应接口模板，提供相同的点对点通信原语（WriteNbi、ReadNbi等），适用于LocalTensor和pipe同步原语不可用的SIMT Kernel侧代码。普通接口通过`channel`逐条提交读、写、写通知和原子操作任务；批量接口通过[MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄添加任务，再通过[BatchCommit](./SIMT_BatchCommit.md)统一提交。批量任务还支持通过Group协作组由组内多个线程协作提交。

通道的底层通信资源包含SQ（Send Queue，发送队列）与CQ（Completion Queue，完成队列）：提交通信任务时，任务描述写入SQ；任务完成后生成完成记录（CQE，Completion Queue Entry）写入CQ，[Drain](./SIMT_Drain.md)通过轮询CQ等待任务完成。任务槽位对应SQ中的WQE（Work Queue Element，工作队列元素），延迟提交的写/读任务占1个任务槽位，立即提交的写/读任务以及写通知/原子任务占2个；任务提交容量即SQ深度（sqDepth），由Host侧创建通道资源时确定。SIMT路径基于`COMM_PROTOCOL_UB_CTP`协议实现，任务通过复制128字节的DWQE（WQE镜像）发布队列门铃（doorbell，通知通信硬件开始处理已组装任务的机制），而非写入标量寄存器。

典型使用流程如下：

1. 创建Hcomm对象并调用[Init](./SIMT_Init.md)完成初始化。
2. 通过对应接口异步提交通信任务。
3. 调用[Drain](./SIMT_Drain.md)阻塞等待通信任务完成。

## 函数原型

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UB_CTP>
class Hcomm;
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commProtocol` | 通信协议类型，当前仅支持`COMM_PROTOCOL_UB_CTP`，默认值为`COMM_PROTOCOL_UB_CTP`，选定协议即确定对应通信路径。 |

## 约束说明

**协议约束**

- SIMT实现当前仅支持`COMM_PROTOCOL_UB_CTP`协议，不支持`COMM_PROTOCOL_ROCE`。

**通用约束**

- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- Hcomm对象为lane（线程）私有：普通接口由单个线程调用；批量Group协作重载中，一个线程规划SQ（Send Queue）范围，组内每个线程写入各自的任务槽位，[BatchCommit](./SIMT_BatchCommit.md)在这些写入就绪后统一发布。
- SIMT路径没有独立的Commit接口。`commit`为`false`的任务驻留在发送队列中，直到后续`commit`为`true`的任务触发提交：该任务的门铃发布的生产者索引覆盖整批任务。
- 同一通道不能同时被本类和SIMD的`AscendC::Hcomm`驱动。

## 调用示例

Hcomm接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，普通接口完整样例参见[04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md)，批量提交完整样例参见[06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md)。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;

__simt_callee__ void SimtWriteOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBuffer)
{
    // 创建SIMT Hcomm对象并初始化，COMM_PROTOCOL_UB_CTP协议无需临时工作区
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 本端localBuffer数据写入远端remoteBuffer
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // 阻塞等待写任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchWriteOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // 添加当前批次任务并提交
    hcomm.WriteNbi(batchHandle, remoteBase, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // 复用批量句柄添加并提交下一批任务
    hcomm.WriteNbi(batchHandle, remoteBase + BYTES, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // 等待累计提交任务生成的全部完成记录
    hcomm.Drain(batchHandle);
}
```
