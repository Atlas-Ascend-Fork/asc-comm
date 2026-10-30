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

头文件路径：`"hcomm/hcomm.h"`。

`AscendC::Hcomm`是AI Core侧点对点通信接口模板，提供普通接口和批量接口两种调用链。普通接口通过`ChannelHandle`提交单个读、写、写通知、原子操作和归约写任务，该调用链需先通过[Init](./Init.md)提供临时工作区，并通过[Commit](./Commit.md)和[Drain](./Drain.md)控制任务提交与完成等待。BatchHandle批量接口支持添加多个任务后统一提交：`COMM_PROTOCOL_UB_CTP`协议支持单通道或多通道批量（多通道模式下多个逻辑通道共享同一个批量句柄及底层通信资源），`COMM_PROTOCOL_ROCE`协议仅支持单通道批量；该调用链不依赖`Init`，其UB（Unified Buffer，统一缓冲区）工作区由[MakeBatchHandle](./MakeBatchHandle.md)提供，完成等待使用批量[Drain](./Drain.md)。

通道的底层通信资源包含SQ（Send Queue，发送队列）与CQ（Completion Queue，完成队列）：提交通信任务时，任务描述写入SQ；任务完成后生成完成记录（CQE）写入CQ，[Drain](./Drain.md)通过轮询CQ等待任务完成。文档中的任务槽位对应SQ中的64字节WQE（Work Queue Element，工作队列元素）Block，任务提交容量即SQ深度（sqDepth），完成记录容量即CQ深度（cqDepth），均由Host侧创建通道资源时确定。

## 函数原型

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UB_CTP>
class Hcomm;
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commProtocol` | 通信协议类型，支持`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UB_CTP`，默认值为`COMM_PROTOCOL_UB_CTP`，选定协议即确定对应通信路径。 |

批量接口支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。单通道模式直接使用[MakeBatchHandle](./MakeBatchHandle.md)返回的句柄添加任务，需要向多个通道提交任务时为每个通道分别创建批量句柄；多通道模式仅`COMM_PROTOCOL_UB_CTP`协议支持，多个逻辑通道共享同一个批量句柄及底层通信资源，通过[GetHandleRef](./GetHandleRef.md)选择逻辑通道并添加任务，通过`MakeBatchHandle`返回的多通道批量句柄执行[BatchCommit](./BatchCommit.md)和批量[Drain](./Drain.md)，`COMM_PROTOCOL_ROCE`不支持多通道模式。

## 返回值说明

返回状态码的接口通常以`0`表示成功、非`0`值表示失败。`Drain`在`COMM_PROTOCOL_UB_CTP`协议下还会直接返回完成记录轮询的详细错误码，用于区分轮询超时和完成状态错误。`MakeBatchHandle`直接返回批量句柄，不返回状态码。各接口的具体返回值见对应API文档。

## 约束说明

**协议约束**

**表2** 普通接口协议支持情况

| 接口 | `COMM_PROTOCOL_UB_CTP` | `COMM_PROTOCOL_ROCE` |
| --- | --- | --- |
| `Init` | 支持 | 支持 |
| `Lock` | 支持 | 支持 |
| `Unlock` | 支持 | 支持 |
| `ReadNbi` | 支持 | 支持 |
| `WriteNbi` | 支持 | 支持 |
| `WriteValueNbi` | 支持 | 不支持 |
| `WriteWithNotifyNbi` | 支持 | 不支持 |
| `AtomicFAA` | 支持 | 不支持 |
| `AtomicCAS` | 支持 | 不支持 |
| `WriteReduceNbi` | 支持 | 不支持 |
| `Commit` | 支持 | 支持 |
| `Drain` | 支持 | 支持 |

**表3** 批量接口协议支持情况

| 接口 | `COMM_PROTOCOL_UB_CTP` | `COMM_PROTOCOL_ROCE` |
| --- | --- | --- |
| `MakeBatchHandle` | 支持 | 支持 |
| `GetHandleRef` | 支持 | 支持 |
| `WriteNbi` | 支持 | 支持 |
| `ReadNbi` | 支持 | 支持 |
| `WriteWithNotifyNbi` | 支持 | 不支持 |
| `BatchCommit` | 支持 | 支持 |
| `Drain` | 支持 | 支持 |

传入的`ChannelHandle`需要指向与所选协议匹配的通道实体。

**通用约束**

- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通接口或并发使用多个BatchHandle，详细约束见[MakeBatchHandle](./MakeBatchHandle.md)和[Drain](./Drain.md)。

各接口的工作区要求、任务提交容量、完成记录配置等细节约束见对应API文档，如[Init](./Init.md)、[WriteNbi](./WriteNbi.md)、[Commit](./Commit.md)。

多通道模式的Host侧句柄创建接口见[MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)。该接口提供HCCL通信域和Hcomm Endpoint两种重载。

## 调用示例

Hcomm接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，普通接口完整样例参见[01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md)，批量接口完整样例参见[02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md)。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

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
// 单批添加的写任务数量
constexpr uint32_t TASK_NUM = 4U;
// 批量工作区大小：按64字节任务槽划分，需容纳整批任务的槽位（槽位总数需小于通道的任务提交容量）
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 声明批量UB工作区，批量接口不依赖Init；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // 创建Hcomm对象
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // 由UB工作区起始地址构造批量工作区LocalTensor
    AscendC::LocalTensor<uint8_t> batchBuffer(
        AscendC::TPosition::VECOUT,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

    // 创建单通道批量句柄并绑定批量工作区，remoteBuffer用于选择远端注册内存
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // 向当前批次依次添加TASK_NUM个写任务，分别写入远端不同偏移；任务先积攒在批量工作区，尚未提交
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.WriteNbi(batchHandle, remoteBuffer + i * BYTES, localBuffer, BYTES);
    }

    // 一次提交当前批次积攒的全部任务
    hcomm.BatchCommit(batchHandle);

    // 阻塞等待整批任务生成的全部完成记录
    hcomm.Drain(batchHandle);
}
```
