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

等待已提交的通信任务完成。接口提供普通`channel`重载、批量`batchHandle`重载和完成集`set`重载：普通重载阻塞等待普通通道上已提交任务完成；批量重载等待批量句柄已提交任务生成的完成记录；完成集重载等待完成集中已准备通道的任务完成。

## 函数原型

普通接口：

```cpp
template <auto pipe = 0>
__simt_callee__ inline int32_t Drain(ChannelHandle channel);
```

批量接口：

```cpp
template <auto pipe = 0>
__simt_callee__ inline int32_t Drain(UbcCtpBatchHandle& batchHandle);
```

完成集接口：

```cpp
__simt_callee__ inline int32_t Drain(UbcCtpCompletionSet& set);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `pipe` | 等待使用的pipe标识，默认值为0。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄。 |
| `set` | 输入/输出 | [InitCompletionSet](./SIMT_InitCompletionSet.md)初始化的完成集。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| 非`0` | 等待失败。 |

## 约束说明

**普通接口**

- 该重载等待普通`channel`调用链产生的完成记录。

**批量接口**

- 调用前必须通过[BatchCommit](./SIMT_BatchCommit.md)提交当前批次中的全部任务。
- 使用批量句柄期间需要独占其关联的通道资源。

**完成集接口**

- 调用前需要通过[InitCompletionSet](./SIMT_InitCompletionSet.md)初始化完成集，并通过[PrepareCompletion](./SIMT_PrepareCompletion.md)登记预期完成的通道。

## 调用示例

Drain接口的调用示例如下。

普通接口：

```cpp
__simt_callee__ void SimtDrainOp(AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer,
    __gm__ uint8_t* remoteBuffer)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 提交写任务
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, 1024U);

    // 阻塞等待普通通道上已提交任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 批量工作区大小
constexpr uint32_t BATCH_BUF_SIZE = 128U;

__simt_callee__ void SimtBatchDrainOp(
    AscendC::ChannelHandle channel, __ubuf__ uint8_t* batchBuffer, __gm__ uint8_t* localBuffer,
    __gm__ uint8_t* remoteBase)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, BATCH_BUF_SIZE, remoteBase);
    hcomm.WriteNbi(batchHandle, remoteBase, localBuffer, 1024U);
    hcomm.BatchCommit(batchHandle);

    // 等待通过BatchCommit提交的任务生成的完成记录
    hcomm.Drain(batchHandle);
}
```
