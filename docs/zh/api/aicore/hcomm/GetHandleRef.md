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
| `remoteAddr` | 输入 | 多通道模式下用于选择远端注册内存。传入`nullptr`时选择该逻辑通道的第一个远端注册buffer；单通道时忽略。默认值为`nullptr`。 |

## 返回值说明

返回用于添加批量任务的BatchHandle引用。

## 约束说明

- 多通道模式下，`channelIndex`必须小于通过[MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)创建`MultiChannelHandle`时指定的`channelNum`。
- 多通道模式下，`remoteAddr`非空时必须位于所选逻辑通道的一个远端注册buffer内。通过返回句柄发起的批量操作必须访问该注册buffer。
- 切换逻辑通道或远端注册buffer时，需要重新调用本接口。
- 对同一个多通道批量句柄多次调用本接口时，返回的引用均指向同一个内层BatchHandle。每次调用都会更新该句柄当前选择的逻辑通道和远端注册内存；再次调用后，之前保存的引用也表示新的选择，不能继续按原选择使用。
- 本接口不返回状态码，调用方必须保证逻辑通道索引和远端地址有效。
- 多通道模式下，通过返回的句柄引用调用批量读、写和写通知接口；通过[MakeBatchHandle](./MakeBatchHandle.md)返回的多通道批量句柄调用[BatchCommit](./BatchCommit.md)和批量[Drain](./Drain.md)。
- 单通道模式下，`channelIndex`和`remoteAddr`不起作用，远端注册内存由`MakeBatchHandle`选择。

## 调用示例

GetHandleRef接口的调用示例如下。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不得小于64B，起始地址需按32B对齐
constexpr uint32_t BATCH_BUF_SIZE = 128U;

extern "C" __global__ __aicore__ void hcomm_multi_channel_write_kernel(
    __gm__ void* multiChannel, GM_ADDR localBuffer, GM_ADDR remoteBuffers,
    uint32_t channelNum)
{
    // 准备批量UB工作区，批量接口不依赖Init
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuf;
    pipe.InitBuffer(batchBuf, BATCH_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> batchBuffer = batchBuf.Get<uint8_t>();

    // 创建Hcomm对象
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // 创建多通道批量句柄
    auto multiBatchHandle = hcomm.MakeBatchHandle(
        multiChannel, batchBuffer, BATCH_BUF_SIZE);

    // 逐个逻辑通道选择远端注册内存，并通过返回的句柄引用添加写任务
    for (uint32_t channelIndex = 0U; channelIndex < channelNum; ++channelIndex) {
        auto& peerBatchHandle = hcomm.GetHandleRef(
            multiBatchHandle, channelIndex, remoteBuffers[channelIndex]);
        hcomm.WriteNbi(peerBatchHandle, remoteBuffers[channelIndex], localBuffer, BYTES);
    }

    // 通过多通道批量句柄提交并等待全部任务完成
    hcomm.BatchCommit(multiBatchHandle);
    hcomm.Drain(multiBatchHandle);
}
```
