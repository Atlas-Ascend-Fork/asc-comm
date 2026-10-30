# GetHandleRef

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

获取用于添加批量任务的BatchHandle引用。多通道模式下，本接口用于选择一个逻辑通道及其远端注册内存；单通道模式下返回传入的句柄本身。

## 函数原型

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline BatchHandle<T>& GetHandleRef(
    T& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr = nullptr);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./MakeBatchHandle.md)创建的单通道或多通道批量句柄。 |
| `channelIndex` | 输入 | 多通道逻辑通道索引；单通道时忽略。 |
| `remoteAddr` | 输入 | 多通道模式下用于选择远端注册内存。传入`nullptr`时选择该逻辑通道的第一个远端注册内存；单通道时忽略。默认值为`nullptr`。 |

## 返回值说明

返回用于添加批量任务的BatchHandle引用。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- 多通道模式下，`channelIndex`必须小于通过[MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)创建`MultiChannelHandle`时指定的`channelNum`。
- 多通道模式下，`remoteAddr`非空时必须位于所选逻辑通道的一个远端注册内存内。通过返回句柄发起的批量操作必须访问该注册内存。
- 切换逻辑通道或远端注册内存时，需要重新调用本接口。
- 对同一个多通道批量句柄多次调用本接口时，返回的引用均指向同一个内层BatchHandle。每次调用都会更新该句柄当前选择的逻辑通道和远端注册内存；再次调用后，之前保存的引用也表示新的选择，不能继续按原选择使用。
- 本接口不返回状态码，调用方必须保证逻辑通道索引和远端地址有效。
- 多通道模式下，通过返回的句柄引用调用批量读、写和写通知接口；通过[MakeBatchHandle](./MakeBatchHandle.md)返回的多通道批量句柄调用[BatchCommit](./BatchCommit.md)和批量[Drain](./Drain.md)。
- 单通道模式下，`channelIndex`和`remoteAddr`不起作用，远端注册内存由`MakeBatchHandle`选择。
- `COMM_PROTOCOL_ROCE`协议下，本接口为直通实现，返回传入的批量句柄本身，`channelIndex`与`remoteAddr`不起作用。多通道批量仅`COMM_PROTOCOL_UB_CTP`协议支持，`COMM_PROTOCOL_ROCE`不支持多通道模式。

## 调用示例

GetHandleRef接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 逻辑通道数量上限，按Host侧创建通道数量调整，channelNum不得超过该值
constexpr uint32_t MAX_CHANNEL_NUM = 8U;
// 批量工作区大小：按64字节任务槽划分，需容纳每个逻辑通道各一个写任务（槽位总数需小于通道的任务提交容量）
constexpr uint32_t BATCH_BUF_SIZE = MAX_CHANNEL_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_multi_channel_write_kernel(
    uint64_t multiChannel, GM_ADDR localBuffer, __gm__ uint64_t* remoteBuffers,
    uint32_t channelNum)
{
    // 声明批量UB工作区，批量接口不依赖Init；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // 创建Hcomm对象
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // 由UB工作区起始地址构造批量工作区LocalTensor
    AscendC::LocalTensor<uint8_t> batchBuffer(
        AscendC::TPosition::VECOUT,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

    // 创建多通道批量句柄
    auto multiBatchHandle = hcomm.MakeBatchHandle(
        multiChannel, batchBuffer, BATCH_BUF_SIZE);

    // 逐个逻辑通道取远端注册内存地址，选择逻辑通道并通过返回的句柄引用添加写任务
    for (uint32_t channelIndex = 0U; channelIndex < channelNum; ++channelIndex) {
        GM_ADDR peerRemoteAddr = reinterpret_cast<GM_ADDR>(remoteBuffers[channelIndex]);
        auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, peerRemoteAddr);
        hcomm.WriteNbi(peerBatchHandle, peerRemoteAddr, localBuffer, BYTES);
    }

    // 通过多通道批量句柄提交并等待全部任务完成
    hcomm.BatchCommit(multiBatchHandle);
    hcomm.Drain(multiBatchHandle);
}
```
