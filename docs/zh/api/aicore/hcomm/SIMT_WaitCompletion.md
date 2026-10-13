# WaitCompletion

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

阻塞轮询等待完成集中指定通道在本轮[PrepareCompletion](./SIMT_PrepareCompletion.md)登记的任务全部完成。`maxIdlePolls`限制连续空轮询的最大次数，超过后返回失败，默认值为1000000。

## 函数原型

```cpp
__simt_callee__ inline int32_t WaitCompletion(
    UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t maxIdlePolls = 1000000U);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `set` | 输入/输出 | [InitCompletionSet](./SIMT_InitCompletionSet.md)初始化的完成集。 |
| `channelIndex` | 输入 | 通道在完成集通道数组中的下标。 |
| `maxIdlePolls` | 输入 | 允许的最大空轮询次数，默认值为1000000。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 指定通道本轮登记的任务全部完成。 |
| 非`0` | 等待失败或超过空轮询上限。 |

## 约束说明

- 调用前需要通过[PrepareCompletion](./SIMT_PrepareCompletion.md)登记该通道的预期完成量。
- `channelIndex`必须小于完成集初始化时的通道数量`count`。
- 阻塞等待期间，SIMT线程不得被其他流程占用。

## 调用示例

WaitCompletion接口的调用示例如下。

```cpp
__simt_callee__ void SimtWaitCompletionOp(
    AscendC::simt::Hcomm<>& hcomm, AscendC::simt::UbcCtpCompletionSet& completionSet,
    uint32_t channelIndex, uint32_t wqeCount, uint32_t bbCount)
{
    // 登记预期完成量后，阻塞等待该通道任务全部完成
    hcomm.PrepareCompletion(completionSet, channelIndex, wqeCount, bbCount);
    hcomm.WaitCompletion(completionSet, channelIndex);
}
```
