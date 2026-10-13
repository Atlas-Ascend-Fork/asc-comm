# InitCompletionSet

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

初始化完成集：绑定通道数组与完成队列工作区，供后续完成轮询接口（[PrepareCompletion](./SIMT_PrepareCompletion.md)、[Progress](./SIMT_Progress.md)、[WaitCompletion](./SIMT_WaitCompletion.md)、[Drain](./SIMT_Drain.md)完成集重载）使用。完成集是SIMT线程轮询通信任务完成状态的载体。

## 函数原型

```cpp
__simt_callee__ inline int32_t InitCompletionSet(
    UbcCtpCompletionSet& set, __gm__ ChannelHandle* channels, uint32_t count, __gm__ uint8_t* workspace,
    uint64_t bytes);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `set` | 输出 | 完成集对象。 |
| `channels` | 输入 | 通道句柄数组，包含`count`个元素。数组下标对应完成集接口的`channelIndex`。 |
| `count` | 输入 | 通道数量。 |
| `workspace` | 输入 | 完成队列工作区，GM地址。 |
| `bytes` | 输入 | 工作区大小，单位为字节，不能小于`128 * (1 + 2 * count)`。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 初始化成功。 |
| 非`0` | 初始化失败。 |

## 约束说明

- `channels`数组内的通道句柄必须有效。
- 完成队列工作区由调用方提供，大小不能小于`128 * (1 + 2 * count)`字节，用于最坏情况下每个通道独占一个完成队列的完成记录存储。
- 工作区在完成集使用期间必须保持有效，不得提前释放或复用。

## 调用示例

InitCompletionSet接口的调用示例如下。

```cpp
// 通道数量
constexpr uint32_t CHANNEL_NUM = 4U;

__simt_callee__ void SimtInitCompletionSetOp(
    AscendC::ChannelHandle* channels, __gm__ uint8_t* completionWorkspace)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 初始化完成集，绑定通道数组与完成队列工作区
    AscendC::simt::UbcCtpCompletionSet completionSet;
    hcomm.InitCompletionSet(completionSet, channels, CHANNEL_NUM, completionWorkspace,
        128ULL * (1ULL + 2ULL * CHANNEL_NUM));
}
```
