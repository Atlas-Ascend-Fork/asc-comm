# BatchCommit

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

提交BatchHandle当前批次中的全部任务。

## 函数原型

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./MakeBatchHandle.md)创建的单通道或多通道批量句柄。成功后，当前批次可以重新添加任务。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- 调用前需要通过批量[WriteNbi](./WriteNbi.md)、[ReadNbi](./ReadNbi.md)或[WriteWithNotifyNbi](./WriteWithNotifyNbi.md)（仅`COMM_PROTOCOL_UB_CTP`协议）至少添加一个任务。
- `COMM_PROTOCOL_UB_CTP`协议下一个批次内可以混合添加读、写和写通知任务；`COMM_PROTOCOL_ROCE`协议仅支持批量读和写任务。
- 当前批次使用的工作区不能超过创建BatchHandle时指定的`buffLen`，并且任务数量必须满足通道容量限制（任务提交容量即SQ深度sqDepth，完成记录容量即CQ深度cqDepth）。
- 多通道模式下，通过[GetHandleRef](./GetHandleRef.md)返回的句柄引用添加任务，通过[MakeBatchHandle](./MakeBatchHandle.md)返回的多通道批量句柄调用本接口。
- 多通道批量仅`COMM_PROTOCOL_UB_CTP`协议支持，`COMM_PROTOCOL_ROCE`不支持多通道模式。
- 提交成功后，可以复用BatchHandle和工作区添加并提交下一个批次。
- 可以连续提交多个批次，再统一调用一次批量[Drain](./Drain.md)。调用方必须保证累计未完成任务数量和完成记录数量不超过通道配置的相应容量。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通`ChannelHandle`提交流程。

## 调用示例

BatchCommit接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 单批添加的写任务数量
constexpr uint32_t TASK_NUM = 4U;
// 批量工作区大小：按64字节任务槽划分，需容纳整批任务的槽位（槽位总数需小于通道的任务提交容量）
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_kernel(
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
