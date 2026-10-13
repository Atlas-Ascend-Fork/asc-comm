# Drain

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

阻塞等待指定普通通道的通信任务完成，或等待BatchHandle已提交任务生成的完成记录。

普通重载依赖`Init`提供的临时工作区；批量重载使用[MakeBatchHandle](./MakeBatchHandle.md)绑定的工作区，不依赖`Init`。

## 函数原型

普通接口：

```cpp
template <pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t Drain(AscendC::ChannelHandle channel);
```

批量接口：

```cpp
template <
    pipe_t pipe = PIPE_MTE3,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t Drain(T& batchHandle);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `pipe` | drain使用的pipe，默认`PIPE_MTE3`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./MakeBatchHandle.md)创建的单通道或多通道批量句柄，用于等待该句柄已提交任务生成的完成记录。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| `-1` | 参数或BatchHandle状态无效，或对应协议返回通用失败。 |
| `0xFF` | `COMM_PROTOCOL_UB_CTP`协议路径下，完成队列轮询超时。 |
| 除`0xFF`外的其他正值 | `COMM_PROTOCOL_UB_CTP`协议路径下，完成记录报告错误；返回值高8位为`status`，低8位为`substatus`。 |

## 约束说明

**普通接口**

- 调用前需要通过`Init`提供临时工作区。
- 该重载等待普通`ChannelHandle`调用链产生的完成记录。

**批量接口**

- 当前仅支持`COMM_PROTOCOL_UB_CTP`协议路径，不需要调用`Init`。
- 多通道模式下，将`MakeBatchHandle`返回的多通道批量句柄传入批量`Drain`，逻辑通道选择参见[GetHandleRef](./GetHandleRef.md)。
- 调用前必须通过[BatchCommit](./BatchCommit.md)提交当前批次中的全部任务。存在尚未提交的任务时调用将返回`-1`。
- 可以执行多次`BatchCommit`后统一调用一次批量`Drain`，接口会等待该BatchHandle累计提交任务生成的全部完成记录。
- 若所有已提交任务均配置为`cqe = 0`，本接口会直接返回，但不能据此确认这些任务已经完成。如需通过本接口确认整批任务完成，需要为能够代表整批完成的任务配置`cqe = 1`。默认配置`URMA_DEFAULT_CFG`的`cqe`为`1`，因此每个任务都会生成完成记录。
- BatchHandle使用期间需要独占其关联的通道资源，不能混用普通`Drain`。

## 调用示例

Drain接口的调用示例如下。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void hcomm_drain_kernel(
    __gm__ void* channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 提交写任务
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // 阻塞等待通道上已提交任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不得小于64B，起始地址需按32B对齐
constexpr uint32_t BATCH_BUF_SIZE = 128U;

extern "C" __global__ __aicore__ void hcomm_batch_drain_kernel(
    __gm__ void* channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备批量UB工作区，批量接口不依赖Init
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuf;
    pipe.InitBuffer(batchBuf, BATCH_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> batchBuffer = batchBuf.Get<uint8_t>();

    // 创建Hcomm对象
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // 创建批量句柄并绑定批量工作区
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);
    hcomm.WriteNbi(batchHandle, remoteBuffer, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // 等待通过BatchCommit提交的任务生成的完成记录
    hcomm.Drain(batchHandle);
}
```
