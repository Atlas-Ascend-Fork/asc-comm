# WriteNbi

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

将数据从本端`src`写入远端`dst`。接口提供普通`channel`重载和批量`batchHandle`重载：普通重载直接向通道提交写任务；批量重载将写任务添加到当前批次，后续由[BatchCommit](./BatchCommit.md)统一提交。

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
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t WriteNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。批量接口不提供该参数。 |
| `commitPipe` | 普通接口commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 普通接口请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA任务配置。默认为`URMA_DEFAULT_CFG`。批量接口要求`inlineEn = 0`，且`cqe`只能为`0`或`1`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./MakeBatchHandle.md)创建的单通道批量句柄，或[GetHandleRef](./GetHandleRef.md)返回的BatchHandle引用。 |
| `dst` | 输出 | 远端目的GM绝对地址。 |
| `src` | 输入 | 本端源GM绝对地址。 |
| `len` | 输入 | 写入长度，单位为字节。必须小于 256 MB（256 * 1024 * 1024 字节）。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或写任务成功添加到当前批次。 |
| `-1` | 操作失败。批量接口中包括批量工作区剩余空间不足。 |

## 约束说明

**普通接口**

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- `COMM_PROTOCOL_UB_CTP`协议路径下，`dst`需要落在通道注册的远端buffer范围内，`src`为本端源地址。
- 若`commit`模板参数设为`false`，连续积攒的任务槽位不得超过通道的任务提交容量；需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录（即中间任务的`config.cqe`设为0，最后一次设为1）。

**批量接口**

- 对批量句柄，`[dst, dst + len)`必须属于其选中的远端注册内存。
- 批量工作区剩余空间不足时返回`-1`。
- 添加完成后需要调用[BatchCommit](./BatchCommit.md)。多通道模式通过[GetHandleRef](./GetHandleRef.md)返回的句柄引用添加任务，并通过[MakeBatchHandle](./MakeBatchHandle.md)返回的多通道批量句柄提交。使用期间需要独占关联的通道资源。

## 调用示例

WriteNbi接口的调用示例如下。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void hcomm_write_nbi_kernel(
    __gm__ void* channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 本端localBuffer数据写入远端remoteBuffer
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // 阻塞等待写任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不得小于64B，起始地址需按32B对齐
constexpr uint32_t BATCH_BUF_SIZE = 128U;

extern "C" __global__ __aicore__ void hcomm_batch_write_nbi_kernel(
    __gm__ void* channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备批量UB工作区，批量接口不依赖Init
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuf;
    pipe.InitBuffer(batchBuf, BATCH_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> batchBuffer = batchBuf.Get<uint8_t>();

    // 创建Hcomm对象
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // 创建单通道批量句柄并绑定批量工作区，remoteBuffer用于选择远端注册内存
    auto batchHandle = hcomm.MakeBatchHandle(
        channel, batchBuffer, BATCH_BUF_SIZE, remoteBuffer);

    // 向当前批次添加写任务
    hcomm.WriteNbi(batchHandle, remoteBuffer, localBuffer, BYTES);

    // 提交当前批次中的全部任务
    hcomm.BatchCommit(batchHandle);

    // 等待已提交任务生成的完成记录
    hcomm.Drain(batchHandle);
}
```
