# AtomicFAA

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

提交Fetch-and-add原子操作任务，对远端地址`dst`中的值执行原子加，并将加法前的旧值写入本端`fetchAddr`。

当前该接口仅支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 函数原型

```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t AtomicFAA(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T addVal);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 原子操作数据类型，仅支持`int32_t`、`uint32_t`、`int64_t`、`uint64_t`。 |
| `commit` | 是否在提交任务时立即commit。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置，仅URMA路径使用。默认为`URMA_DEFAULT_CFG`（强序 + fence + 使能CQE）。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输入/输出 | 远端原子操作目标GM地址。 |
| `fetchAddr` | 输出 | 本端GM地址，用于保存远端地址执行原子加前的旧值。 |
| `addVal` | 输入 | 加到远端目标地址的值。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前通信通道需已完成初始化。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UB_CTP`通道。
- `dst`需要落在通道注册的远端buffer范围内，`fetchAddr`用于保存旧值，长度为`sizeof(T)`。
- 单个`AtomicFAA`任务在URMA SQ中占用2个WQE block。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

## 调用示例

AtomicFAA接口的调用示例如下。

```cpp
// 原子加的值
constexpr uint64_t ADD_VAL = 1U;
// 初始化UB临时工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void hcomm_atomic_faa_kernel(
    __gm__ void* channel, GM_ADDR remoteCounter, GM_ADDR fetchBuffer)
{
    // 准备UB临时工作区
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 对远端remoteCounter执行原子加ADD_VAL，加法前的旧值写入本端fetchBuffer
    hcomm.AtomicFAA<uint64_t>(channel, remoteCounter, fetchBuffer, ADD_VAL);

    // 阻塞等待原子操作任务完成
    hcomm.Drain(channel);
}
```
