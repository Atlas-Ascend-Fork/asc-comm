# AtomicCAS

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持URMA协议，不支持RoCE协议
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
<!-- npu="x90" id7 -->
- Kirin X90：不支持
<!-- end id7 -->

## 功能说明

提交Compare-and-swap原子操作任务，比较远端地址`dst`中的值和`compareVal`，相等时将远端值替换为`swapVal`，并将替换前的旧值写入本端`fetchAddr`。

当前该接口仅支持`COMM_PROTOCOL_UBC_CTP`路径。

## 函数原型

```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t AtomicCAS(
    AscendC::ChannelHandle channel,
    GM_ADDR dst,
    GM_ADDR fetchAddr,
    T compareVal,
    T swapVal);
```

## 参数说明

**表1**  模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 原子操作数据类型，仅支持`int32_t`、`uint32_t`、`int64_t`、`uint64_t`。 |
| `commit` | 是否在提交任务时立即commit。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置。默认为`URMA_DEFAULT_CFG`(执行序为relax order + CQE保序上报 + 使能fence + 需要上报CQE)。 |

**表2**  接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输入/输出 | 远端原子操作目标地址。 |
| `fetchAddr` | 输出 | 本端地址，用于保存远端地址执行CAS前的旧值。 |
| `compareVal` | 输入 | 比较值。 |
| `swapVal` | 输入 | 比较成功时写入远端目标地址的新值。 |

## 返回值说明

| 返回值 | 描述 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前通信通道需已完成初始化。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UBC_CTP`通道。
- `dst`需要落在通道注册的远端buffer范围内，`fetchAddr`用于保存旧值，长度为`sizeof(T)`。
- 单个`AtomicCAS`任务在URMA SQ中占用2个WQE block。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

## 调用示例

AtomicCAS接口的调用示例如下。

```cpp
int32_t oldVal = 0;
hcomm.AtomicCAS(channel, remoteAddr, reinterpret_cast<GM_ADDR>(&oldVal),
                 /*compareVal=*/0, /*swapVal=*/1);
hcomm.Drain(channel);    // 等待任务完成
```
