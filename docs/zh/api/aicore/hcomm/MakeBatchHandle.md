# MakeBatchHandle

## 功能说明

创建与通信协议匹配的批量句柄，并绑定用于批量准备WQE的UB缓冲区。当前批量句柄仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

接口通过channel类型映射得到对应的BatchHandle返回类型，调用侧建议使用`auto`接收返回值。当前`ChannelHandle`映射为执行句柄`UbcCtpBatchHandle`；`MultiChannelHandle`映射为用于选择逻辑通道的`UbcCtpMultiBatchHandle`，通过`GetHandleRef`取得其中的`UbcCtpBatchHandle`执行句柄。

## 函数原型

```cpp
template <typename T, typename U>
__aicore__ inline BatchHandle<T> MakeBatchHandle(
    T channel,
    const AscendC::LocalTensor<U>& buff,
    uint32_t buffLen,
    GM_ADDR remoteAddr = nullptr,
    GM_ADDR localAddr = nullptr);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | UBC_CTP单通道`ChannelHandle`，或Host侧`MakeMultiChannelHandle`创建的共享Jetty `MultiChannelHandle`。 |
| `buff` | 输入 | 调用方提供的UB `LocalTensor`，用于准备批量WQE，并由批量`Drain`复用为CQE临时缓冲区。 |
| `buffLen` | 输入 | 缓冲区可用长度，单位为字节。 |
| `remoteAddr` | 输入 | 单通道模式下用于选择远端已注册buffer；多通道模式下为预留参数，通过`GetHandleRef`选择远端MR。默认值为`nullptr`。 |
| `localAddr` | 输入 | 本端内存地址，默认值为`nullptr`。当前版本为预留参数，暂不用于缓存本端注册内存信息。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 通信通道句柄类型，由`channel`实参推导；当前支持`ChannelHandle`和`MultiChannelHandle`。 |
| `U` | `LocalTensor`的元素类型。 |

## 返回值

返回`BatchHandle<T>`：`ChannelHandle`对应`UbcCtpBatchHandle`，`MultiChannelHandle`对应
`UbcCtpMultiBatchHandle`。仅`COMM_PROTOCOL_UBC_CTP`支持该接口。

该接口不通过返回码报告参数错误。调用方必须满足下述约束；调试模式下，非法参数会触发接口断言。

## 约束说明

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`批量句柄。
- 单通道模式下，`channel`不能为`0`，且需要指向已完成资源初始化的UBC_CTP通道实体。`remoteAddr`非空时必须落在某个远端已注册buffer范围内；为空时，通道必须至少包含一个远端已注册buffer。接口缓存选中buffer的`tokenId/tokenValue`，后续批量接口不再查询远端注册表。
- 多通道模式下，`channel`不能是空`MultiChannelHandle`，且其共享队列实体和远端信息数组必须有效。`remoteAddr`不参与创建，通过`GetHandleRef`选择逻辑通道和远端MR。
- `localAddr`当前未参与地址校验或token缓存。
- `buff`起始地址需要按32字节对齐，`buffLen`不能小于64字节。
- `buffLen`不能超过`buff.GetSize() * sizeof(U)`。
- `buffLen`必须严格小于通道SQ容量，即`sqDepth * 64`字节。可准备的WQEBB数量为`buffLen / 64`，不能整除的剩余空间不会用于存放WQE。
- BatchHandle缓存创建时的SQ/CQ上下文和队列计数。BatchHandle有效期间，调用方需要独占对应单通道或共享Jetty，不能交叉调用普通`ChannelHandle`接口，也不能并发使用多个BatchHandle。
- BatchHandle本身、通道资源、`MultiChannelHandle`及`buff`的生命周期由调用方负责。在BatchHandle使用完成前，不得释放或并发复用这些资源。
- BatchHandle应作为逻辑上的不透明句柄使用，不建议调用方直接访问或修改其成员。

## 调用示例

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>();
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen, remoteAddr);

auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferLen);
auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, peerRemoteAddr);
```

多通道模式下，调用[GetHandleRef](./GetHandleRef.md)选择逻辑通道，通过返回的`BatchHandle<ChannelHandle>`引用准备WQE，并对该内层引用执行`BatchCommit`和批量`Drain`。`multiBatchHandle`只用于后续的`GetHandleRef`调用。

## 相关接口

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [GetHandleRef](./GetHandleRef.md)
