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

头文件路径：`"hcomm/hcomm.h"`。

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
    pipe_t pipe = PIPE_S,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t Drain(T& batchHandle);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `pipe` | drain使用的pipe，普通接口默认`PIPE_MTE3`，批量接口默认`PIPE_S`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./MakeBatchHandle.md)创建的单通道或多通道批量句柄，用于等待该句柄已提交任务生成的完成记录。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| `-1` | 参数或BatchHandle状态无效，或对应协议返回通用失败。 |
| `0xFF` | `COMM_PROTOCOL_UB_CTP`协议下，完成队列轮询超时。 |
| 除`0xFF`外的其他正值 | `COMM_PROTOCOL_UB_CTP`协议下，完成记录报告错误；返回值高8位为`status`，低8位为`substatus`。 |

**表4** `status`取值说明

| `status`取值 | 说明 |
| --- | --- |
| `0x00` | 无错误。 |
| `0x01` | 不支持的操作码。 |
| `0x02` | 本端操作错误。 |
| `0x03` | 远端操作错误。 |
| `0x04` | 事务重试次数超限。 |
| `0x05` | 事务ACK超时。 |
| `0x06` | 工作请求被冲刷（非异常）。 |
| 其他取值 | 保留。 |

**表5** `substatus`取值说明

| `status`取值 | `substatus`取值 | 说明 |
| --- | --- | --- |
| `0x02` | `0x01` | 本端长度错误。 |
| `0x02` | `0x02` | 本端访问错误。 |
| `0x02` | `0x03` | 远端响应长度错误。 |
| `0x02` | `0x04` | 本端数据损坏。 |
| `0x03` | `0x01` | 远端不支持的请求。 |
| `0x03` | `0x02` | 远端访问中止。 |
| `0x03` | `0x04` | 远端数据损坏。 |

其他`status`取值下，`substatus`无含义，默认为`0`。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。

**普通接口**

- 调用前需要通过`Init`提供临时工作区。
- 该重载等待普通`ChannelHandle`调用链产生的完成记录。

**批量接口**

- 不需要调用`Init`。
- 多通道模式下，将`MakeBatchHandle`返回的多通道批量句柄传入批量`Drain`，逻辑通道选择参见[GetHandleRef](./GetHandleRef.md)。
- 多通道批量仅`COMM_PROTOCOL_UB_CTP`协议支持，`COMM_PROTOCOL_ROCE`不支持多通道模式。
- 调用前必须通过[BatchCommit](./BatchCommit.md)提交当前批次中的全部任务。存在尚未提交的任务时调用将返回`-1`。
- 可以执行多次`BatchCommit`后统一调用一次批量`Drain`，接口会等待该BatchHandle累计提交任务生成的全部完成记录。
- 若所有已提交任务均配置为`cqe = 0`，本接口会直接返回，但不能据此确认这些任务已经完成。如需通过本接口确认整批任务完成，需要为能够代表整批完成的任务配置`cqe = 1`。默认配置`URMA_DEFAULT_CFG`的`cqe`为`1`，因此每个任务都会生成完成记录。
- BatchHandle使用期间需要独占其关联的通道资源，不能混用普通`Drain`。

## 调用示例

Drain接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md)与[02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md)（批量）。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_drain_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

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
// 单批添加的写任务数量
constexpr uint32_t TASK_NUM = 4U;
// 批量工作区大小：按64字节任务槽划分，需容纳整批任务的槽位（槽位总数需小于通道的任务提交容量）
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_drain_kernel(
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

    // 创建批量句柄并绑定批量工作区
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // 向当前批次依次添加TASK_NUM个写任务，分别写入远端不同偏移
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.WriteNbi(batchHandle, remoteBuffer + i * BYTES, localBuffer, BYTES);
    }
    hcomm.BatchCommit(batchHandle);

    // 等待整批任务生成的全部完成记录
    hcomm.Drain(batchHandle);
}
```
