# GetHandleRef

## 功能说明

从共享Jetty多通道批量句柄中选择一个逻辑通道，返回用于批量读、写或写通知接口的通道引用。

不同逻辑通道的引用共享父句柄中的批量队列状态，并由父句柄统一执行`BatchCommit`和批量`Drain`。

## 函数原型

```cpp
template <typename T>
__aicore__ inline BatchHandleRef<typename BatchHandleTraits<T>::ChannelType>
GetHandleRef(T& batchHandle, uint32_t channelIndex);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | 由`MakeBatchHandle(MultiChannelHandle, ...)`创建的多通道批量句柄。返回的引用会指向其中的共享批量状态。 |
| `channelIndex` | 输入 | 逻辑通道索引，与Host侧`MakeMultiChannelHandle`传入的`channelDescs`数组下标一致。 |

## 返回值

返回`BatchHandleRef<MultiChannelHandle>`，当前实际类型为`UbcCtpBatchHandleRef`。

当父句柄无效、UB缓冲区容量为0、`channelIndex`越界或远端信息地址无效时，返回无效引用，其共享状态指针为`nullptr`。

## 约束说明

- `batchHandle`必须由有效的`MultiChannelHandle`创建，不能传入单通道`UbcCtpBatchHandle`。
- 返回值引用父句柄中的共享状态，因此父句柄及其UB缓冲区必须保持有效，且不同通道引用不能并发准备WQE。
- `BatchCommit`和`Drain`必须传入父多通道批量句柄，不能传入该接口返回值。

## 相关接口

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
