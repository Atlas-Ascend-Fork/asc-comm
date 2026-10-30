# WriteReduceNbi

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

提交带归约操作的写任务：将本端`src`的`count`个`T`类型元素与远端`dst`中的对应元素按`reduceOp`执行归约，结果写入远端`dst`。

## 函数原型

```cpp
template <
    typename T, HcommUrmaReduceOp reduceOp, bool commit = true, pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3, auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteReduceNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 归约操作的数据类型，仅支持`int8_t`、`int16_t`、`int32_t`、`uint32_t`、`half`、`float`、`bfloat16_t`。 |
| `reduceOp` | 归约操作类型（`HcommUrmaReduceOp`），支持`MAX`、`MIN`、`SUM`。 |
| `commit` | 是否在提交任务时立即commit。`false`表示延迟提交，后续由`commit`为`true`的调用统一触发。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | WQE控制配置，默认`URMA_DEFAULT_CFG`（执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE）。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输出 | 远端归约目标GM（Global Memory，全局内存）地址。 |
| `src` | 输入 | 本端源GM地址。 |
| `count` | 输入 | 归约的元素数量，总字节数为`count * sizeof(T)`。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 调用前通信通道需已完成初始化，并通过[Init](./Init.md)提供临时工作区。
- `dst`需要落在通道注册的远端注册内存范围内。
- `config.inlineEn`必须为0，否则编译报错。
- 若`commit`模板参数设为`false`，连续积攒的任务槽位不得超过通道的任务提交容量（即SQ深度sqDepth）；需在容量耗尽前通过[Commit](./Commit.md)或自动commit提交。

## 调用示例

WriteReduceNbi接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理。

```cpp
// 归约的元素数量
constexpr uint64_t COUNT = 256U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_reduce_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 以SUM归约将本端localBuffer的COUNT个float元素聚合写入远端remoteBuffer
    hcomm.WriteReduceNbi<float, AscendC::HcommUrmaReduceOp::SUM>(
        channel, remoteBuffer, localBuffer, COUNT);

    // 阻塞等待归约写任务完成
    hcomm.Drain(channel);
}
```