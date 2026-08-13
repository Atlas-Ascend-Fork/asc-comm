# MakeBatchHandle

## 功能说明

创建与通信协议匹配的批量句柄，并绑定用于批量准备WQE的UB缓冲区。当前批量句柄仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

接口通过channel类型映射得到对应的BatchHandle返回类型，调用侧建议使用`auto`接收返回值。当前
`ChannelHandle`映射为`UbcCtpBatchHandle`。

## 函数原型

```cpp
template <typename T>
__aicore__ inline BatchHandle<T> MakeBatchHandle(
    T channel,
    __ubuf__ uint8_t* buff,
    uint32_t buffLen,
    GM_ADDR dstBase = nullptr,
    GM_ADDR srcBase = nullptr);

template <typename T, typename U>
__aicore__ inline BatchHandle<T> MakeBatchHandle(
    T channel,
    const AscendC::LocalTensor<U>& buff,
    uint32_t buffLen,
    GM_ADDR dstBase = nullptr,
    GM_ADDR srcBase = nullptr);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | UBC_CTP通信通道句柄。 |
| `buff` | 输入 | 调用方提供的UB缓冲区或`LocalTensor`，用于准备批量WQE，并由批量`Drain`复用为CQE临时缓冲区。 |
| `buffLen` | 输入 | 缓冲区可用长度，单位为字节。 |
| `dstBase` | 输入 | 与批量句柄绑定的远端已注册内存基地址，默认值为`nullptr`。接口根据该地址查找并缓存远端token。当前UBC_CTP实现要求其命中远端已注册buffer。 |
| `srcBase` | 输入 | 本端源内存基地址，默认值为`nullptr`。当前版本为预留参数，暂不用于缓存本端注册内存信息。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 通信通道句柄类型，由`channel`实参推导；当前支持`ChannelHandle`。 |
| `U` | `LocalTensor`的元素类型，仅用于`LocalTensor`重载。 |

## 返回值

返回`BatchHandle<T>`。当前`ChannelHandle`对应的实际返回类型为`UbcCtpBatchHandle`，且仅
`COMM_PROTOCOL_UBC_CTP`支持该接口。

该接口不通过返回码报告参数错误。调用方必须满足下述约束；调试模式下，非法参数会触发接口断言。

## 约束说明

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`批量句柄。
- `channel`不能为`0`，且需要指向已完成资源初始化的UBC_CTP通道实体。
- `dstBase`必须落在`channel`的某个远端已注册buffer范围内。接口缓存该buffer对应的token，后续批量读写接口不会再次查询远端注册表。
- `srcBase`当前未参与地址校验或token缓存。
- `buff`起始地址需要按32字节对齐，`buffLen`不能小于64字节。
- 使用`LocalTensor`重载时，`buffLen`不能超过`buff.GetSize() * sizeof(U)`。
- `buffLen`必须严格小于通道SQ容量，即`sqDepth * 64`字节。可准备的WQEBB数量为`buffLen / 64`，不能整除的剩余空间不会用于存放WQE。
- BatchHandle缓存创建时的SQ/CQ上下文和队列计数。BatchHandle有效期间，调用方需要独占对应通道，不能在同一通道上交叉调用普通`ChannelHandle`接口，也不能并发使用多个BatchHandle。
- BatchHandle本身、通道资源及`buff`的生命周期由调用方负责。在BatchHandle使用完成前，不得释放或并发复用这些资源。
- BatchHandle应作为逻辑上的不透明句柄使用，不建议调用方直接访问或修改其成员。

## 调用示例

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen, dstBase);
```
