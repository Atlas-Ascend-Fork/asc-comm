# GetHandleRef

## 功能说明

从共享Jetty多通道批量句柄中选择一个逻辑通道，返回用于批量读、写或写通知接口的BatchHandle引用。

父多通道批量句柄统一执行`BatchCommit`和批量`Drain`。

## 函数原型

```cpp
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    BatchHandle<MultiChannelHandle>& batchHandle,
    uint32_t channelIndex);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | 由`MakeBatchHandle(MultiChannelHandle, ...)`创建的多通道批量句柄。 |
| `channelIndex` | 输入 | 逻辑通道索引，与Host侧`MakeMultiChannelHandle`传入的`channelDescs`数组下标一致。 |

## 返回值

返回`BatchHandle<ChannelHandle>&`，用于在所选逻辑通道上准备批量WQE。

## 约束说明

- `batchHandle`必须由有效的`MultiChannelHandle`创建，不能传入单通道`UbcCtpBatchHandle`。
- `channelIndex`必须小于创建`MultiChannelHandle`时传入的`channelNum`。
- 使用返回值调用批量读、写或写通知接口。
- `BatchCommit`和`Drain`必须传入父多通道批量句柄，不能传入该接口返回值。

## 相关接口

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
