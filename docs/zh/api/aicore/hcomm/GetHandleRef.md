# GetHandleRef

## 功能说明

从共享Jetty多通道批量句柄中选择一个逻辑通道和远端注册内存，返回统一执行批量读、写、写通知、提交和等待接口的BatchHandle引用。

## 函数原型

```cpp
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    BatchHandle<MultiChannelHandle>& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | 由`MakeBatchHandle(MultiChannelHandle, ...)`创建的多通道批量句柄。 |
| `channelIndex` | 输入 | 逻辑通道索引，与Host侧`MakeMultiChannelHandle`传入的`channelDescs`数组下标一致。 |
| `remoteAddr` | 输入 | 所选逻辑通道的远端已注册buffer内地址。 |

## 返回值

返回配置为所选逻辑通道和远端MR的内层`BatchHandle<ChannelHandle>&`。

## 约束说明

- `batchHandle`必须由有效的`MultiChannelHandle`创建，不能传入单通道`UbcCtpBatchHandle`。
- `channelIndex`必须小于创建`MultiChannelHandle`时传入的`channelNum`。
- `remoteAddr`不能为空，且必须落在所选逻辑通道的一个远端已注册buffer中。
- 对同一个多通道批量句柄多次调用本接口时，返回的引用均指向同一个内层BatchHandle；每次调用会更新该内层句柄当前选择的逻辑通道和远端MR信息。
- 返回引用的批量操作必须访问所选MR；切换MR前需要再次调用本接口。
- 使用返回的内层引用调用批量读、写、写通知、`BatchCommit`和批量`Drain`。
- 多通道批量句柄仅用于调用本接口选择逻辑通道，不能直接传入上述执行接口。

## 相关接口

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
