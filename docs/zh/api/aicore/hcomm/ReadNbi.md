# ReadNbi

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->

## 功能说明

将数据从远端`src`读取到本端`dst`。接口提供普通`ChannelHandle`重载和BatchHandle重载：普通重载直接向通道提交读任务；批量重载将读任务添加到当前批次，后续由`BatchCommit`统一提交。

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
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t ReadNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

## 参数说明

**表1**  模板参数说明

| 参数 | 说明 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。批量接口不提供该参数。 |
| `commitPipe` | 普通接口commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 普通接口请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA任务配置。默认为`URMA_DEFAULT_CFG`。批量接口要求`inlineEn = 0`，且`cqe`只能为`0`或`1`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2**  接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道批量句柄，或`GetHandleRef`返回的BatchHandle引用。 |
| `dst` | 输出 | 本端目的GM绝对地址。 |
| `src` | 输入 | 远端源GM绝对地址。 |
| `len` | 输入 | 读取长度，单位为字节。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或读任务成功添加到当前批次。 |
| `-1` | 操作失败。批量接口中包括UB缓冲区剩余空间不足。 |

## 约束说明

### 普通接口

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- `COMM_PROTOCOL_UBC_CTP`路径下，`src`需要落在通道注册的远端buffer范围内，`dst`为本端目标地址。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

### 批量接口

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。
- 每个批量读任务占用64字节批量工作区。
- 对批量句柄，`[src, src + len)`必须属于其选中的远端注册内存。
- 批量工作区剩余空间不足时返回`-1`。
- 添加完成后需要调用`BatchCommit`。多通道模式通过`GetHandleRef`返回的句柄引用添加任务，并通过`MakeBatchHandle`返回的多通道批量句柄提交。使用期间需要独占关联的通道资源。

## 调用示例

普通接口：

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // 已完成资源初始化的UBC_CTP通道句柄
GM_ADDR localDst; // 有效的本端目的地址
GM_ADDR remoteSrc; // 已注册的远端源地址
uint64_t dataLen = 64U;

int32_t ret = hcomm.ReadNbi(channel, localDst, remoteSrc, dataLen);
if (ret == 0) {
    ret = hcomm.Drain(channel);
}
```

批量接口：

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // 已完成资源初始化的UBC_CTP通道句柄
AscendC::LocalTensor<uint8_t> batchBuffer; // 已从UB内存中分配
GM_ADDR remoteSrc; // 已注册的远端源地址
GM_ADDR localDst; // 有效的本端目的地址
uint32_t batchBufferLen = 64U;
uint32_t dataLen = 64U;

auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteSrc);
int32_t ret = hcomm.ReadNbi(batchHandle, localDst, remoteSrc, dataLen);
if (ret == 0) {
    ret = hcomm.BatchCommit(batchHandle);
}
if (ret == 0) {
    ret = hcomm.Drain(batchHandle);
}
```
