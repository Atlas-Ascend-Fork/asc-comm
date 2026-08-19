# WriteWithNotifyNbi

## 功能说明

提交点对点写任务，并在写任务中携带远端通知地址和通知值。

当前该接口仅支持`COMM_PROTOCOL_UBC_CTP`路径。`COMM_PROTOCOL_ROCE`路径保留同名接口，但实现会返回失败。

## 函数原型

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

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输出 | 目的GM地址。 |
| `src` | 输入 | 源GM地址。 |
| `len` | 输入 | 写入长度，单位为字节。 |
| `notifyAddr` | 输入 | 远端通知地址。 |
| `notifyVal` | 输入 | 远端通知值。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `commit` | 是否在提交任务时立即commit。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置，仅URMA路径使用。默认为`URMA_DEFAULT_CFG`（强序 + fence + 使能CQE）。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前通信通道需已完成初始化。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UBC_CTP`通道。
- `COMM_PROTOCOL_ROCE`路径不支持该接口，调用会返回`-1`。
- 单个`WriteWithNotifyNbi`任务在URMA SQ中占用2个WQE block。
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
