# GetHandleRef

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

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄。 |
| `channelIndex` | 输入 | 多通道逻辑通道索引；单通道时忽略。 |
| `remoteAddr` | 输入 | 多通道模式下用于选择远端注册内存。传入`nullptr`时选择该逻辑通道的第一个远端注册buffer；单通道时忽略。默认值为`nullptr`。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

## 返回值

返回用于添加批量任务的BatchHandle引用。

## 约束说明

- 多通道模式下，`channelIndex`必须小于创建`MultiChannelHandle`时指定的`channelNum`。
- 多通道模式下，`remoteAddr`非空时必须位于所选逻辑通道的一个远端注册buffer内。通过返回句柄发起的批量操作必须访问该注册buffer。
- 切换逻辑通道或远端注册buffer时，需要重新调用本接口。
- 对同一个多通道批量句柄多次调用本接口时，返回的引用均指向同一个内层BatchHandle。每次调用都会更新该句柄当前选择的逻辑通道和远端注册内存；再次调用后，之前保存的引用也表示新的选择，不能继续按原选择使用。
- 本接口不返回状态码，调用方必须保证逻辑通道索引和远端地址有效。
- 多通道模式下，通过返回的句柄引用调用批量读、写和写通知接口；通过`MakeBatchHandle`返回的多通道批量句柄调用`BatchCommit`和批量`Drain`。
- 单通道模式下，`channelIndex`和`remoteAddr`不起作用，远端注册内存由`MakeBatchHandle`选择。

## 相关接口

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
