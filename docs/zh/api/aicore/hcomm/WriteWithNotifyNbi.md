# WriteWithNotifyNbi

## 功能说明

将数据从本端`src`写入远端`dst`，并携带远端通知地址和通知值。接口提供普通`ChannelHandle`重载和BatchHandle重载：普通重载直接向通道提交任务；批量重载只在BatchHandle的UB缓冲区中准备写通知WQE，后续由`BatchCommit`统一提交。

普通和批量重载均仅支持`COMM_PROTOCOL_UBC_CTP`路径。批量重载当前仅支持Ascend 950。

## 函数原型

普通接口：

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteWithNotifyNbi(
    AscendC::ChannelHandle channel,
    GM_ADDR dst,
    GM_ADDR src,
    uint64_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

批量接口：

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteWithNotifyNbi(
    T& batchHandle,
    GM_ADDR dst,
    GM_ADDR src,
    uint32_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的批量句柄。成功准备WQE后，句柄中的WQEBB计数会更新；`cqe = 1`时句柄本地`cqHead`也会增加。 |
| `dst` | 输出 | 远端目的GM绝对地址。对于批量接口，调用方必须保证其访问范围与`batchHandle`缓存的`tokenId/tokenValue`匹配。 |
| `src` | 输入 | 本端源GM绝对地址。 |
| `len` | 输入 | 写入长度，单位为字节。 |
| `notifyAddr` | 输入 | 远端通知GM绝对地址。对于批量接口，调用方必须保证其访问范围与`batchHandle`缓存的`tokenId/tokenValue`匹配。 |
| `notifyVal` | 输入 | 远端通知值。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。批量接口不提供该参数。 |
| `commitPipe` | 普通接口commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 普通接口请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置。默认为`URMA_DEFAULT_CFG`（强序 + fence + 使能CQE）。批量接口要求`inlineEn = 0`且`cqe`只能为`0`或`1`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导；当前支持`UbcCtpBatchHandle`。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或批量WQE准备成功。 |
| `-1` | 操作失败。批量接口中包括UB缓冲区剩余空间不足。`COMM_PROTOCOL_ROCE`普通路径也返回失败。 |

## 约束说明

### 普通接口

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UBC_CTP`通道。
- `COMM_PROTOCOL_ROCE`路径不支持该接口，调用会返回`-1`。
- 单个普通写通知任务在URMA SQ中占用2个WQEBB。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

### 批量接口

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。
- 每个批量写通知任务占用2个64字节WQEBB，只在UB中准备，不复制到GM SQ，也不敲doorbell。
- `config.inlineEn`必须为`0`，`config.cqe`支持`0`或`1`。同一批次的不同读、写、写通知任务可以使用不同的`cqe`配置。
- `dst`和`notifyAddr`不会再次与远端注册内存范围进行校验，也不会再次查询远端注册表。通知上下文复用BatchHandle缓存的`tokenId/tokenValue`；调用方必须保证两个远端访问区间都属于`MakeBatchHandle`根据`remoteAddr`缓存token时所命中的同一注册内存。
- 当缓冲区容量校验失败时返回`-1`，BatchHandle中的WQEBB计数和`cqHead`保持不变。
- 准备完成后需要调用`BatchCommit`。BatchHandle使用期间需要独占对应通道。

## 相关接口

- [MakeBatchHandle](./MakeBatchHandle.md)
- [BatchCommit](./BatchCommit.md)
