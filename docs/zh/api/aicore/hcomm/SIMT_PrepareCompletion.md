# PrepareCompletion

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

在完成集中登记指定通道本轮预期完成的任务量，为完成轮询做准备。`channelIndex`为完成集初始化时通道数组的数组下标，`wqeCount`和`bbCount`分别为本轮预期完成的WQE数量和BB数量。登记后可通过[WaitCompletion](./SIMT_WaitCompletion.md)等待该通道完成，或通过[Progress](./SIMT_Progress.md)轮询推进。

## 函数原型

```cpp
__simt_callee__ inline int32_t PrepareCompletion(
    UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t wqeCount, uint32_t bbCount);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `set` | 输入/输出 | [InitCompletionSet](./SIMT_InitCompletionSet.md)初始化的完成集。 |
| `channelIndex` | 输入 | 通道在完成集通道数组中的下标。 |
| `wqeCount` | 输入 | 本轮预期完成的WQE数量。 |
| `bbCount` | 输入 | 本轮预期完成的BB数量。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 登记成功。 |
| 非`0` | 登记失败。 |

## 约束说明

- `channelIndex`必须小于完成集初始化时的通道数量`count`。
- `wqeCount`和`bbCount`需要与该通道本轮已提交任务的实际预期完成量一致。

## 调用示例

PrepareCompletion接口的调用示例如下。

```cpp
__simt_callee__ void SimtPrepareCompletionOp(
    AscendC::simt::Hcomm<>& hcomm, AscendC::simt::UbcCtpCompletionSet& completionSet,
    uint32_t channelIndex, uint32_t wqeCount, uint32_t bbCount)
{
    // 登记指定通道本轮预期完成的WQE数量与BB数量
    hcomm.PrepareCompletion(completionSet, channelIndex, wqeCount, bbCount);
}
```
