# MakeBatchHandle

## 功能说明

创建批量句柄，并绑定批量操作所需的UB工作区。当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

调用侧建议使用`auto`接收返回值。单通道模式直接使用返回的句柄添加和提交任务；多通道模式通过`GetHandleRef`选择逻辑通道后添加任务。

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
| `channel` | 输入 | UBC_CTP单通道`ChannelHandle`，或Host侧`MakeMultiChannelHandle`创建的`MultiChannelHandle`。 |
| `buff` | 输入 | 调用方提供的UB `LocalTensor`，作为批量操作的工作区。 |
| `buffLen` | 输入 | 工作区可用长度，单位为字节。 |
| `remoteAddr` | 输入 | 单通道模式下用于选择远端注册内存。传入`nullptr`时选择第一个远端注册buffer；多通道模式下忽略。默认值为`nullptr`。 |
| `localAddr` | 输入 | 保留参数，当前版本不使用。默认值为`nullptr`。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 通道句柄类型，支持`ChannelHandle`和`MultiChannelHandle`。 |
| `U` | `LocalTensor`的元素类型。 |

## 返回值

返回与`channel`对应的`BatchHandle<T>`。多通道句柄无效或远端注册内存选择失败时，返回零值的无效批量句柄，不得用于后续批量接口。该接口不返回状态码，调用方必须保证参数满足约束。

## 约束说明

- 仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`。
- `channel`必须有效，并且对应的通信资源已完成初始化。
- 单通道模式下，`remoteAddr`非空时必须位于一个远端注册buffer内。后续批量操作访问的远端地址必须属于该注册buffer。
- 多通道模式下，通过`GetHandleRef`选择逻辑通道和远端注册内存。
- `buff`起始地址必须按32字节对齐。
- `buffLen`不能小于64字节，且不能超过`buff.GetSize() * sizeof(U)`。不能被64整除的剩余空间不可用。
- 一个批次实际占用的64字节任务槽位数量必须小于通道的任务提交容量，否则`BatchCommit`返回`-1`。该容量在Host侧创建通道资源时确定。
- BatchHandle、通道资源、`MultiChannelHandle`和`buff`的生命周期由调用方负责。BatchHandle使用完成前，不得释放或并发复用这些资源。
- 使用BatchHandle期间必须独占其关联的通道资源，不能交叉调用普通`ChannelHandle`接口，也不能并发使用其他BatchHandle。
- BatchHandle应作为不透明句柄使用，调用方不应直接访问或修改其成员。

## 调用示例

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>();

// 单通道
auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteAddr);

// 多通道
auto multiBatchHandle = hcomm.MakeBatchHandle(
    multiChannel, batchBuffer, batchBufferLen);
auto& peerBatchHandle = hcomm.GetHandleRef(
    multiBatchHandle, channelIndex, peerRemoteAddr);

// 多通道模式下，通过`peerBatchHandle`添加批量任务，通过`multiBatchHandle`调用`BatchCommit`和批量`Drain`。
```

## 相关接口

- [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)
- [GetHandleRef](./GetHandleRef.md)
- [BatchCommit](./BatchCommit.md)
- [Drain](./Drain.md)
