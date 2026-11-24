# asccomm

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

头文件路径：`"asccomm/asccomm.h"`。

`asc::asccomm`是AI Core侧点对点通信接口模板，提供普通接口和批量接口两种调用链。普通接口通过`ChannelHandle`添加单个读、写、写通知、原子操作和归约写任务，该调用链需先通过[init](./init.md)提供临时工作区，并通过[commit](./commit.md)和[drain](./drain.md)控制任务提交与完成等待。batch_handle批量接口支持添加多个任务后统一提交：`COMM_PROTOCOL_UB_CTP`协议支持单通道或多通道批量（多通道模式下多个逻辑通道共享同一个批量句柄及底层通信资源），`COMM_PROTOCOL_ROCE`协议仅支持单通道批量；该调用链不依赖`init`，其UB（Unified Buffer，统一缓冲区）工作区由[make_batch_handle](./make_batch_handle.md)提供，完成等待使用批量[drain](./drain.md)。

通道的底层通信资源包含SQ（Send Queue，发送队列）与CQ（Completion Queue，完成队列）：添加通信任务时，任务描述写入SQ；任务完成后生成完成记录（CQE）写入CQ，[drain](./drain.md)通过轮询CQ等待任务完成。文档中的任务槽位对应SQ中的64字节WQE（Work Queue Element，工作队列元素）Block，任务提交容量即SQ深度（sqDepth），完成记录容量即CQ深度（cqDepth），均由Host侧创建通道资源时确定。

## 函数原型

```cpp
template <asc::CommProtocol comm_protocol = asc::COMM_PROTOCOL_UB_CTP>
class asccomm;
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `comm_protocol` | 通信协议类型，支持`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UB_CTP`，默认值为`COMM_PROTOCOL_UB_CTP`，选定协议即确定对应通信路径。 |

批量接口支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。单通道模式直接使用[make_batch_handle](./make_batch_handle.md)返回的句柄添加任务，需要向多个通道提交任务时为每个通道分别创建批量句柄；多通道模式仅`COMM_PROTOCOL_UB_CTP`协议支持，多个逻辑通道共享同一个批量句柄及底层通信资源，通过[get_handle_ref](./get_handle_ref.md)选择逻辑通道并添加任务，通过`make_batch_handle`返回的多通道批量句柄执行[batch_commit](./batch_commit.md)和批量[drain](./drain.md)，`COMM_PROTOCOL_ROCE`不支持多通道模式。

## 返回值说明

返回状态码的接口通常以`0`表示成功、非`0`值表示失败。任务提交类接口（如[write_nbi](./write_nbi.md)、[commit](./commit.md)）正常场景下仅返回`0`，约束不满足时通过调试期断言（Trap）报告错误，不返回错误码；[drain](./drain.md)在轮询超时或完成记录错误时先触发调试期断言（Trap），再返回协议对应的错误码。`make_batch_handle`直接返回批量句柄，不返回状态码。各接口的具体返回值见对应API文档。

## 约束说明

**协议约束**

**表2** 普通接口协议支持情况

| 接口 | `COMM_PROTOCOL_UB_CTP` | `COMM_PROTOCOL_ROCE` |
| --- | --- | --- |
| `init` | 支持 | 支持 |
| `lock` | 支持 | 支持 |
| `unlock` | 支持 | 支持 |
| `read_nbi` | 支持 | 支持 |
| `write_nbi` | 支持 | 支持 |
| `write_value_nbi` | 支持 | 不支持 |
| `write_with_notify_nbi` | 支持 | 不支持 |
| `atomic_faa_nbi` | 支持 | 不支持 |
| `atomic_cas_nbi` | 支持 | 不支持 |
| `write_reduce_nbi` | 支持 | 不支持 |
| `commit` | 支持 | 支持 |
| `drain` | 支持 | 支持 |

**表3** 批量接口协议支持情况

| 接口 | `COMM_PROTOCOL_UB_CTP` | `COMM_PROTOCOL_ROCE` |
| --- | --- | --- |
| `make_batch_handle` | 支持 | 支持 |
| `get_handle_ref` | 支持 | 支持 |
| `write_nbi` | 支持 | 支持 |
| `read_nbi` | 支持 | 支持 |
| `write_with_notify_nbi` | 支持 | 不支持 |
| `batch_commit` | 支持 | 支持 |
| `drain` | 支持 | 支持 |

传入的`ChannelHandle`需要指向与所选协议匹配的通道实体。

**通用约束**

- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- 使用batch_handle期间必须独占其关联的通道资源，不能混用普通接口或并发使用多个batch_handle，详细约束见[make_batch_handle](./make_batch_handle.md)和[drain](./drain.md)。

各接口的工作区要求、任务提交容量、完成记录配置等细节约束见对应API文档，如[init](./init.md)、[write_nbi](./write_nbi.md)、[commit](./commit.md)。

多通道模式的Host侧句柄创建接口见[make_multi_channel_handle](../../host/asccomm/make_multi_channel_handle.md)。该接口提供HCCL通信域和Hcomm Endpoint两种重载。

## 调用示例

asccomm接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，普通接口完整样例参见[01_asccomm_urma_write_read_nbi](../../../../../examples/aicore/asccomm/01_asccomm_urma_write_read_nbi/README.md)，批量接口完整样例参见[02_asccomm_urma_batch_write](../../../../../examples/aicore/asccomm/02_asccomm_urma_batch_write/README.md)。`COMM_PROTOCOL_ROCE`协议的完整样例参见[07_asccomm_roce_normal_write_read](../../../../../examples/aicore/asccomm/07_asccomm_roce_normal_write_read/README.md)（普通接口）与[08_asccomm_roce_batch_write_read](../../../../../examples/aicore/asccomm/08_asccomm_roce_batch_write_read/README.md)（批量接口）。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void write_nbi_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];

    // 创建asccomm对象并初始化UB临时工作区
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    asccomm.init(asccomm_local, ASCCOMM_BUF_SIZE);

    // 本端local_buffer数据写入远端remote_buffer
    asccomm.write_nbi(channel, remote_buffer, local_buffer, BYTES);

    // 阻塞等待写任务完成
    asccomm.drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 单批添加的写任务数量
constexpr uint32_t TASK_NUM = 4U;
// 批量工作区大小：按64字节任务槽划分，需容纳整批任务的槽位（槽位总数需小于通道的任务提交容量）
constexpr uint32_t BATCH_BUF_SIZE = TASK_NUM * 64U;

extern "C" __vector__ __global__ __aicore__ void asccomm_batch_write_nbi_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 声明批量UB工作区，批量接口不依赖Init；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    // 创建asccomm对象
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;

    // 创建单通道批量句柄并绑定批量工作区，remote_buffer用于选择远端注册内存
    auto batch_handle = asccomm.make_batch_handle(
        channel, asccomm_local, BATCH_BUF_SIZE, remote_buffer);

    // 向当前批次依次添加TASK_NUM个写任务，分别写入远端不同偏移；任务先积攒在批量工作区，尚未提交
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        asccomm.write_nbi(batch_handle, remote_buffer + i * BYTES, local_buffer, BYTES);
    }

    // 一次提交当前批次积攒的全部任务
    asccomm.batch_commit(batch_handle);

    // 阻塞等待整批任务生成的全部完成记录
    asccomm.drain(batch_handle);
}
```
