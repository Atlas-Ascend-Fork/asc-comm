# ReadNbi

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

将数据从远端`src`读取到本端`dst`。接口提供普通`channel`重载和批量`batchHandle`重载：普通重载直接向通道提交读任务；批量重载将读任务添加到当前批次，后续由[BatchCommit](./BatchCommit.md)统一提交。

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

批量多SGE（Scatter-Gather Element，离散内存段描述）接口：

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t ReadNbi(T& batchHandle, const BufDesc* dstDescs, uint32_t dstNum, GM_ADDR src);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。批量接口不提供该参数。 |
| `commitPipe` | 普通接口commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 普通接口请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | WQE控制配置，默认为`URMA_DEFAULT_CFG`（执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE）；`COMM_PROTOCOL_ROCE`协议下仅`cqe`与`fence`字段生效。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./MakeBatchHandle.md)创建的单通道批量句柄，或[GetHandleRef](./GetHandleRef.md)返回的BatchHandle引用。 |
| `dst` | 输出 | 本端目的GM（Global Memory，全局内存）绝对地址。 |
| `src` | 输入 | 远端源GM绝对地址。 |
| `len` | 输入 | 读取长度，单位为字节。`COMM_PROTOCOL_UB_CTP`协议下不超过256MB（256 * 1024 * 1024字节），`COMM_PROTOCOL_ROCE`协议下小于2GB（2 * 1024 * 1024 * 1024字节）。 |
| `dstDescs` | 输入 | 本端离散内存段描述数组（多SGE接口），每个元素包含`addr`（段起始GM地址）与`len`（段长度，单位为字节）。 |
| `dstNum` | 输入 | 内存段数量（多SGE接口）。`COMM_PROTOCOL_UB_CTP`协议下取值范围为1-13，`COMM_PROTOCOL_ROCE`协议下为1-24。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或读任务成功添加到当前批次。 |
| `-1` | 操作失败。批量接口中包括批量工作区剩余空间不足。 |

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- `len`为传输长度，单位为字节：`COMM_PROTOCOL_UB_CTP`协议下不超过256MB（256 * 1024 * 1024字节）；`COMM_PROTOCOL_ROCE`协议下小于2GB（2 * 1024 * 1024 * 1024字节）。

**普通接口**

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- `COMM_PROTOCOL_UB_CTP`协议下，`src`需要落在通道注册的远端注册内存范围内，`dst`为本端目标地址。
- 若`commit`模板参数设为`false`，连续积攒的任务槽位不得超过通道的任务提交容量（即SQ深度sqDepth）；需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。
- 延迟提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录（即中间任务的`config.cqe`设为0，最后一次设为1）。

**批量接口**

- 每个批量读任务占用1个任务槽位（64字节）批量工作区。
- 对批量句柄，`[src, src + len)`必须属于其选中的远端注册内存。
- 批量工作区剩余空间不足时返回`-1`。
- 添加完成后需要调用[BatchCommit](./BatchCommit.md)。多通道模式通过[GetHandleRef](./GetHandleRef.md)返回的句柄引用添加任务，并通过[MakeBatchHandle](./MakeBatchHandle.md)返回的多通道批量句柄提交。使用期间需要独占关联的通道资源。
- 多通道批量仅`COMM_PROTOCOL_UB_CTP`协议支持，`COMM_PROTOCOL_ROCE`不支持多通道模式。
- `COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议均支持多SGE接口。多SGE接口中，从远端`src`读取的数据写入`dstDescs`给出的各离散本端内存段，总长度为各段`len`之和；`dstNum`取值范围：`COMM_PROTOCOL_UB_CTP`协议下为1-13，`COMM_PROTOCOL_ROCE`协议下为1-24，`COMM_PROTOCOL_UB_CTP`协议下超出范围任务添加接口返回`-1`，`COMM_PROTOCOL_ROCE`协议下超限为调试期断言；`COMM_PROTOCOL_ROCE`协议下各段`len`及累加总长度均须小于2GB（2 * 1024 * 1024 * 1024字节）。

## 调用示例

ReadNbi接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md)。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_read_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 远端remoteBuffer数据读取到本端localBuffer
    hcomm.ReadNbi(channel, localBuffer, remoteBuffer, BYTES);

    // 阻塞等待读任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 单批添加的读任务数量
constexpr uint32_t TASK_NUM = 4U;
// 批量工作区大小：按64字节任务槽划分，需容纳整批任务的槽位（槽位总数需小于通道的任务提交容量）
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_read_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 声明批量UB工作区，批量接口不依赖Init；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    // 创建Hcomm对象
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // 由UB工作区起始地址构造批量工作区LocalTensor
    AscendC::LocalTensor<uint8_t> batchBuffer(
        AscendC::TPosition::VECOUT,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hcommLocal)), BATCH_BUF_SIZE);

    // 创建单通道批量句柄并绑定批量工作区，remoteBuffer用于选择远端注册内存
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // 向当前批次依次添加TASK_NUM个读任务，分别读取远端不同偏移到本端不同偏移；任务先积攒在批量工作区，尚未提交
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.ReadNbi(batchHandle, localBuffer + i * BYTES, remoteBuffer + i * BYTES, BYTES);
    }

    // 一次提交当前批次积攒的全部任务
    hcomm.BatchCommit(batchHandle);

    // 阻塞等待整批任务生成的全部完成记录
    hcomm.Drain(batchHandle);
}
```
