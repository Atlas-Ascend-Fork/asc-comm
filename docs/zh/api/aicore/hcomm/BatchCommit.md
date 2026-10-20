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

提交BatchHandle当前批次中的全部任务。当前仅支持`COMM_PROTOCOL_UB_CTP`协议路径。

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

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败，例如当前批次为空、状态无效或任务数量超过允许容量。 |

## 约束说明

- 调用前需要通过批量`ReadNbi`、`WriteNbi`或`WriteWithNotifyNbi`至少添加一个任务。
- 一个批次内可以混合添加读、写和写通知任务。
- 当前批次使用的工作区不能超过创建BatchHandle时指定的`buffLen`，并且任务数量必须满足通道容量限制。
- 多通道模式下，通过[GetHandleRef](./GetHandleRef.md)返回的句柄引用添加任务，通过[MakeBatchHandle](./MakeBatchHandle.md)返回的多通道批量句柄调用本接口。
- 提交成功后，可以复用BatchHandle和工作区添加并提交下一个批次。
- 可以连续提交多个批次，再统一调用一次批量[Drain](./Drain.md)。调用方必须保证累计未完成任务数量和完成记录数量不超过通道配置的相应容量。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通`ChannelHandle`提交流程。

## 调用示例

BatchCommit接口的调用示例如下。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不得小于64B，起始地址需按32B对齐
constexpr uint32_t BATCH_BUF_SIZE = 128U;

extern "C" __global__ __aicore__ void hcomm_batch_write_kernel(
    __gm__ void* channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备批量UB工作区，批量接口不依赖Init
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuf;
    pipe.InitBuffer(batchBuf, BATCH_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> batchBuffer = batchBuf.Get<uint8_t>();

    // 创建Hcomm对象
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // 创建单通道批量句柄并绑定批量工作区，remoteBuffer用于选择远端注册内存
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // 向当前批次添加写任务
    hcomm.WriteNbi(batchHandle, remoteBuffer, localBuffer, BYTES);

    // 提交当前批次中的全部任务
    hcomm.BatchCommit(batchHandle);

    // 等待已提交任务生成的完成记录
    hcomm.Drain(batchHandle);
}
```
