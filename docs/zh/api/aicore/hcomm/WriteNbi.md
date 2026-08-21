# WriteNbi

## 功能说明

将数据从本端`src`写入远端`dst`。接口提供普通`ChannelHandle`重载和BatchHandle重载：普通重载直接向通道提交写任务；批量重载只在BatchHandle的UB缓冲区中准备写WQE，后续由`BatchCommit`统一提交。

## 函数原型

普通接口：

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

批量接口：

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的批量句柄。成功准备WQE后，句柄中的WQEBB计数会更新；`cqe = 1`时句柄本地`cqHead`也会增加。 |
| `dst` | 输出 | 远端目的GM绝对地址。对于批量接口，调用方必须保证`[dst, dst + len)`属于`batchHandle`缓存的`tokenId/tokenValue`所代表的注册内存。 |
| `src` | 输入 | 本端源GM绝对地址。 |
| `len` | 输入 | 写入长度，单位为字节。 |

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
| `-1` | 操作失败。批量接口中包括UB缓冲区剩余空间不足。 |

## 约束说明

### 普通接口

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- `COMM_PROTOCOL_UBC_CTP`路径下，`dst`需要落在通道注册的远端buffer范围内，`src`为本端源地址。

### 批量接口

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。
- 每个批量写任务占用1个64字节WQEBB，只在UB中准备，不复制到GM SQ，也不敲doorbell。
- `config.inlineEn`必须为`0`，`config.cqe`支持`0`或`1`。同一批次的不同读、写、写通知任务可以使用不同的`cqe`配置。
- `dst`不会再次与远端注册内存范围进行校验，也不会再次查询远端注册表。调用方必须保证`[dst, dst + len)`属于`MakeBatchHandle`根据`remoteAddr`缓存`tokenId/tokenValue`时所命中的同一注册内存。
- 当缓冲区容量校验失败时返回`-1`，BatchHandle中的WQEBB计数和`cqHead`保持不变。
- 准备完成后需要调用`BatchCommit`。BatchHandle使用期间需要独占对应通道。

## 相关样例

[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README.md)演示普通接口的AIV直驱URMA两卡读写流程。BatchHandle基本流程见[Hcomm使用说明](../../../guide/hcomm_usage.md)。
