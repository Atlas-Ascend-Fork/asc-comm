# GetHandleRef

## 功能说明

返回用于执行批量操作的BatchHandle引用。单通道时原样返回传入句柄；多通道时选择一个逻辑通道和远端注册内存。

## 函数原型

```cpp
template <
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    T& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr = nullptr);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄。 |
| `channelIndex` | 输入 | 多通道逻辑通道索引；单通道时忽略。 |
| `remoteAddr` | 输入 | 多通道远端已注册buffer内地址；为空时选择该通道的第一个远端已注册buffer。单通道时忽略。默认值为`nullptr`。 |

## 返回值

单通道返回输入句柄本身；多通道返回配置为所选逻辑通道和远端MR的内层`BatchHandle<ChannelHandle>&`。

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

## 约束说明

- 单通道时`channelIndex`和`remoteAddr`不改变句柄，远端内存仍由`MakeBatchHandle`选择。
- 多通道时，`channelIndex`必须小于创建`MultiChannelHandle`时传入的`channelNum`。
- 多通道时，`remoteAddr`非空时必须落在所选逻辑通道的一个远端已注册buffer中；为空时选择该通道的第一个远端已注册buffer。
- 本接口不返回状态码，调用方必须保证多通道的逻辑通道索引和远端MR地址有效。
- 对同一个多通道批量句柄多次调用本接口时，返回的引用均指向同一个内层BatchHandle；每次调用会更新该内层句柄当前选择的逻辑通道和远端MR信息。
- 多通道返回引用的批量操作必须使用本次调用缓存的token；访问使用不同token的远端buffer前需要再次调用本接口。
- 多通道使用返回的内层引用调用批量读、写和写通知；将外层多通道批量句柄传入`BatchCommit`和批量`Drain`。

## 相关接口

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
