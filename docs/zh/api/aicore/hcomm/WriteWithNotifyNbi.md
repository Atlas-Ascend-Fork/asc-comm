# WriteWithNotifyNbi

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

将数据从本端`src`写入远端`dst`，并携带远端通知地址和通知值。接口提供普通`channel`重载和批量`batchHandle`重载：普通重载直接向通道提交任务；批量重载将写通知任务添加到当前批次，后续由[BatchCommit](./BatchCommit.md)统一提交。

普通和批量重载均仅支持`COMM_PROTOCOL_UB_CTP`协议。

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
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteWithNotifyNbi(
    T& batchHandle,
    GM_ADDR dst,
    GM_ADDR src,
    uint32_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

批量多SGE（Scatter-Gather Element，离散内存段描述）接口：

```cpp
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteWithNotifyNbi(
    T& batchHandle,
    GM_ADDR dst,
    const BufDesc* srcDescs,
    uint32_t srcNum,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。批量接口不提供该参数。 |
| `commitPipe` | 普通接口commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 普通接口请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | WQE控制配置，默认为`URMA_DEFAULT_CFG`（执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE）。|
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./MakeBatchHandle.md)创建的单通道批量句柄，或[GetHandleRef](./GetHandleRef.md)返回的BatchHandle引用。 |
| `dst` | 输出 | 远端目的GM（Global Memory，全局内存）绝对地址。 |
| `src` | 输入 | 本端源GM绝对地址。 |
| `len` | 输入 | 写入长度，单位为字节。必须不超过256MB（256 * 1024 * 1024字节）。 |
| `notifyAddr` | 输入 | 远端通知GM绝对地址。批量接口中，该地址必须与`dst`属于同一远端注册内存。 |
| `notifyVal` | 输入 | 远端通知值。 |
| `srcDescs` | 输入 | 本端离散内存段描述数组（批量多SGE接口），每个元素包含`addr`（段起始GM地址）与`len`（段长度，单位为字节）。 |
| `srcNum` | 输入 | 内存段数量（批量多SGE接口）。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或写通知任务成功添加到当前批次。 |
| `-1` | 操作失败。批量接口中包括批量工作区剩余空间不足。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。

**普通接口**

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UB_CTP`通道。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`的一半（单个任务占用2个任务槽位）；需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。
- 延迟提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录（即中间任务的`config.cqe`设为0，最后一次设为1）。

**批量接口**

- `dst`访问区间和`notifyAddr`必须属于创建或选择BatchHandle时指定的同一远端注册内存。
- 批量多SGE接口将`srcDescs`给出的各离散内存段按数组顺序聚合，连续写入远端`dst`起始的区间，写完成后向`notifyAddr`写入通知值。`srcNum`取值范围为1-11，超出范围任务添加失败；各数据段地址与长度的有效性由调用方保证。
- 添加完成后需要调用[BatchCommit](./BatchCommit.md)。多通道模式通过[GetHandleRef](./GetHandleRef.md)返回的句柄引用添加任务，并通过[MakeBatchHandle](./MakeBatchHandle.md)返回的多通道批量句柄提交。使用期间需要独占关联的通道资源。

## 调用示例

WriteWithNotifyNbi接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 写任务完成后写入远端通知地址的通知值
constexpr uint64_t NOTIFY_VAL = 1U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_with_notify_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer,
    GM_ADDR notifyAddr)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 本端localBuffer数据写入远端remoteBuffer，任务完成后向远端notifyAddr写入通知值
    // 单个普通写通知任务占用2个任务提交槽位
    hcomm.WriteWithNotifyNbi(
        channel, remoteBuffer, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);

    // 阻塞等待写通知任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 写任务完成后写入远端通知地址的通知值
constexpr uint64_t NOTIFY_VAL = 1U;
// 单批添加的写通知任务数量
constexpr uint32_t TASK_NUM = 4U;
// 批量工作区大小：单个批量写通知任务占用2个任务槽位（128字节），需容纳整批任务（槽位总数需小于通道的任务提交容量）
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 128U;

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_with_notify_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer,
    GM_ADDR notifyAddr)
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

    // 向当前批次依次添加TASK_NUM个写通知任务，分别写入远端不同偏移，完成后均向notifyAddr写入通知值
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        hcomm.WriteWithNotifyNbi(
            batchHandle, remoteBuffer + i * BYTES, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);
    }

    // 一次提交当前批次积攒的全部任务
    hcomm.BatchCommit(batchHandle);

    // 阻塞等待整批任务生成的全部完成记录
    hcomm.Drain(batchHandle);
}
```