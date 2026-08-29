# ReadNbi

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：支持RoCE协议，不支持URMA协议
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：支持RoCE协议，不支持URMA协议
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：支持RoCE协议，不支持URMA协议
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品AI Core：支持RoCE协议，不支持URMA协议
- Atlas 推理系列产品Vector Core：支持RoCE协议，不支持URMA协议
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：支持RoCE协议，不支持URMA协议
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90：支持RoCE协议，不支持URMA协议
<!-- end id7 -->

## 功能说明

将数据从远端`src`读取到本端`dst`。接口提供普通`ChannelHandle`重载和BatchHandle重载：普通重载直接向通道提交读任务；批量重载只在BatchHandle的UB缓冲区中准备读WQE，后续由`BatchCommit`统一提交。

## 函数原型

普通接口：

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t ReadNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

批量接口：

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t ReadNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

## 参数说明

**表1**  模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。批量接口不提供该参数。 |
| `commitPipe` | 普通接口commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 普通接口请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置。默认为`URMA_DEFAULT_CFG`(执行序为relax order + CQE保序上报 + 使能fence + 需要上报CQE)。批量接口要求`inlineEn = 0`且`cqe`只能为`0`或`1`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2**  接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道批量句柄，或`GetHandleRef`返回的BatchHandle引用。 |
| `dst` | 输出 | 本端目的GM绝对地址。 |
| `src` | 输入 | 远端源GM绝对地址。 |
| `len` | 输入 | 读取长度，单位为字节。 |

## 返回值说明

| 返回值 | 描述 |
| --- | --- |
| `0` | 普通任务提交成功，或批量WQE准备成功。 |
| `-1` | 操作失败。批量接口中包括UB缓冲区剩余空间不足。 |

## 约束说明

### 普通接口

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- `COMM_PROTOCOL_UBC_CTP`路径下，`src`需要落在通道注册的远端buffer范围内，`dst`为本端目标地址。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

### 批量接口

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。
- 每个批量读任务占用1个64字节WQEBB，只在UB中准备，不复制到GM SQ，也不敲doorbell。
- `config.inlineEn`必须为`0`，`config.cqe`支持`0`或`1`。同一批次的不同读、写、写通知任务可以使用不同的`cqe`配置。
- 对批量句柄，`[src, src + len)`必须属于其选中的远端注册内存。
- 当缓冲区容量校验失败时返回`-1`，BatchHandle中的WQEBB计数和`cqHead`保持不变。
- 准备完成后需要调用`BatchCommit`。多通道模式通过`GetHandleRef`返回的内层BatchHandle引用准备WQE，并通过外层`UbcCtpMultiBatchHandle`提交。使用期间需要独占对应单通道或共享Jetty。

## 调用示例

ReadNbi接口的调用示例如下。

```cpp
// 普通接口
hcomm.ReadNbi(channel, readDst, readSrc, DATA_SIZE);
hcomm.Drain(channel);

// 批量接口
auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, batchBufferLen);
hcomm.ReadNbi(batchHandle, dst, src, len);
hcomm.BatchCommit(batchHandle);
hcomm.Drain(batchHandle);
```
