# Progress

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

非阻塞推进完成集内各通道的完成队列处理。`budget`为本轮最多处理的完成记录数量，`processed`输出本轮实际处理的完成记录数量。与[WaitCompletion](./SIMT_WaitCompletion.md)的阻塞等待不同，本接口立即返回，适合在轮询循环中使用。

## 函数原型

```cpp
__simt_callee__ inline int32_t Progress(UbcCtpCompletionSet& set, uint32_t budget, uint32_t& processed);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `set` | 输入/输出 | [InitCompletionSet](./SIMT_InitCompletionSet.md)初始化的完成集。 |
| `budget` | 输入 | 本轮最多处理的完成记录数量。 |
| `processed` | 输出 | 本轮实际处理的完成记录数量。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 推进成功。 |
| 非`0` | 推进失败。 |

## 约束说明

- 调用前需要通过[PrepareCompletion](./SIMT_PrepareCompletion.md)登记预期完成的通道。
- 本接口非阻塞，需要配合轮询循环使用，直至`processed`达到预期或通过[WaitCompletion](./SIMT_WaitCompletion.md)完成等待。

## 调用示例

Progress接口的调用示例如下。

```cpp
__simt_callee__ void SimtProgressOp(
    AscendC::simt::Hcomm<>& hcomm, AscendC::simt::UbcCtpCompletionSet& completionSet)
{
    // 轮询推进完成集处理，直至本轮预算内的完成记录全部处理完毕
    uint32_t processed = 0U;
    do {
        hcomm.Progress(completionSet, 32U, processed);
    } while (processed > 0U);
}
```
