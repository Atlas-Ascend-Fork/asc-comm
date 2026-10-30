# AtomicCAS

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

头文件路径：`"hcomm/hcomm.h"`。

提交Compare-and-swap原子操作任务，比较远端地址`dst`中的值和`compareVal`，相等时将远端值替换为`swapVal`，并将替换前的旧值写入本端`fetchAddr`。

当前该接口仅支持`COMM_PROTOCOL_UB_CTP`协议。

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

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 原子操作数据类型，仅支持`int32_t`、`uint32_t`、`int64_t`、`uint64_t`。 |
| `commit` | 是否在提交任务时立即commit。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | WQE控制配置，默认为`URMA_DEFAULT_CFG`（执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE）。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输出 | 远端原子操作目标GM（Global Memory，全局内存）地址。 |
| `fetchAddr` | 输出 | 本端GM地址，用于保存远端地址执行CAS前的旧值。 |
| `compareVal` | 输入 | 比较值。 |
| `swapVal` | 输入 | 比较成功时写入远端目标地址的新值。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UB_CTP`通道。
- `dst`需要落在通道注册的远端注册内存范围内，`fetchAddr`用于保存旧值，长度为`sizeof(T)`。
- 单个`AtomicCAS`任务占用2个任务槽位（128字节）。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`的一半（单个任务占用2个任务槽位），需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续任务提交将因SQ溢出而失败。
- 延迟提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

## 调用示例

AtomicCAS接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理。

```cpp
// 比较值
constexpr uint64_t COMPARE_VAL = 0U;
// 比较成功时写入远端目标地址的新值
constexpr uint64_t SWAP_VAL = 1U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_atomic_cas_kernel(
    uint64_t channel, GM_ADDR remoteValue, GM_ADDR fetchBuffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 远端remoteValue与COMPARE_VAL相等时替换为SWAP_VAL，替换前的旧值写入本端fetchBuffer
    hcomm.AtomicCAS<uint64_t>(
        channel, remoteValue, fetchBuffer, COMPARE_VAL, SWAP_VAL);

    // 阻塞等待原子操作任务完成
    hcomm.Drain(channel);
}
```