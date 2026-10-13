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

提交批量句柄当前批次中的全部任务。接口提供单线程批量重载和Group协作批量重载：Group协作批量重载由组内多个SIMT线程协作完成批次提交。

## 函数原型

单线程批量接口：

```cpp
__simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle);
```

Group协作批量接口：

```cpp
template <typename Group>
__simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle, const Group& group);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄。成功后，当前批次可以重新添加任务。 |
| `group` | 输入 | SIMT协作组，Group协作批量重载使用。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| 非`0` | 提交失败。 |

## 约束说明

- 调用前需要通过批量接口（如[WriteNbi](./SIMT_WriteNbi.md)、[ReadNbi](./SIMT_ReadNbi.md)）至少添加一个任务。
- 提交成功后，可以复用BatchHandle和工作区添加并提交下一个批次。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通`ChannelHandle`提交流程。
- 批量任务的完成等待通过[Drain](./SIMT_Drain.md)的批量重载完成。
- 同一有效批量句柄下，`itemBb`为1的操作（[WriteNbi](./SIMT_WriteNbi.md)、[ReadNbi](./SIMT_ReadNbi.md)、[WriteValueNbi](./SIMT_WriteValueNbi.md)）与`itemBb`为2的操作（[WriteWithNotifyNbi](./SIMT_WriteWithNotifyNbi.md)、[AtomicFAA](./SIMT_AtomicFAA.md)、[AtomicCAS](./SIMT_AtomicCAS.md)）不能混用。

## 调用示例

BatchCommit接口的调用示例如下。

```cpp
// 批量工作区大小
constexpr uint32_t BATCH_BUF_SIZE = 128U;

__simt_callee__ void SimtBatchCommitOp(
    AscendC::ChannelHandle channel, __ubuf__ uint8_t* batchBuffer, __gm__ uint8_t* localBuffer,
    __gm__ uint8_t* remoteBase)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, BATCH_BUF_SIZE, remoteBase);
    hcomm.WriteNbi(batchHandle, remoteBase, localBuffer, 1024U);

    // 提交当前批次中的全部任务
    hcomm.BatchCommit(batchHandle);

    // 等待已提交任务生成的完成记录
    hcomm.Drain(batchHandle);
}
```
