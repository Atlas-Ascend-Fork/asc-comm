# drain

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

阻塞等待指定普通通道的通信任务完成，或等待batch_handle已提交任务生成的完成记录。

普通重载依赖`init`提供的临时工作区；批量重载使用[make_batch_handle](./make_batch_handle.md)绑定的工作区，不依赖`init`。

## 函数原型

普通接口：

```cpp
template <pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t drain(ChannelHandle channel);
```

批量接口：

```cpp
template <
    pipe_t pipe = PIPE_S,
    typename T,
    typename handle_traits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t drain(T& batch_handle);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `pipe` | drain使用的pipe，普通接口默认`PIPE_MTE3`，批量接口默认`PIPE_S`。 |
| `T` | 批量句柄类型，由`batch_handle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batch_handle` | 输入/输出 | [make_batch_handle](./make_batch_handle.md)创建的单通道或多通道批量句柄，用于等待该句柄已提交任务生成的完成记录。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| `-1` | `COMM_PROTOCOL_ROCE`协议下，完成记录轮询失败时返回的通用失败码。 |
| `0xFF` | `COMM_PROTOCOL_UB_CTP`协议下，完成队列轮询超时。 |
| 除`0xFF`外的其他正值 | `COMM_PROTOCOL_UB_CTP`协议下，完成记录报告错误；返回值高8位为`status`，低8位为`substatus`。 |

完成记录轮询超时或完成记录报告错误时，接口先触发调试断言（Trap），再返回对应错误码。

**表4** `status`取值说明

| `status`取值 | 说明 |
| --- | --- |
| `0x00` | 无错误。 |
| `0x01` | 不支持的操作码。 |
| `0x02` | 本端操作错误。 |
| `0x03` | 远端操作错误。 |
| `0x04` | 事务重试次数超限。 |
| `0x05` | 事务ACK超时。 |
| `0x06` | 工作请求被冲刷（非异常）。 |
| 其他取值 | 保留。 |

**表5** `substatus`取值说明

| `status`取值 | `substatus`取值 | 说明 |
| --- | --- | --- |
| `0x02` | `0x01` | 本端长度错误。 |
| `0x02` | `0x02` | 本端访问错误。 |
| `0x02` | `0x03` | 远端响应长度错误。 |
| `0x02` | `0x04` | 本端数据损坏。 |
| `0x03` | `0x01` | 远端不支持的请求。 |
| `0x03` | `0x02` | 远端访问中止。 |
| `0x03` | `0x04` | 远端数据损坏。 |

其他`status`取值下，`substatus`无含义，默认为`0`。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- `COMM_PROTOCOL_ROCE`协议下，完成记录轮询超时或完成记录报告错误时，接口先触发调试断言（Trap），再返回`-1`；`COMM_PROTOCOL_UB_CTP`协议下同样先触发调试断言（Trap），再返回`0xFF`（轮询超时）或`status`/`substatus`组合值（完成记录错误），取值详见返回值说明。

**普通接口**

- 调用前需要通过`init`提供临时工作区。
- 该重载等待普通`ChannelHandle`调用链产生的完成记录。

**批量接口**

- 不需要调用`init`。
- 多通道模式下，将`make_batch_handle`返回的多通道批量句柄传入批量`drain`，逻辑通道选择参见[get_handle_ref](./get_handle_ref.md)。
- 多通道批量仅`COMM_PROTOCOL_UB_CTP`协议支持，`COMM_PROTOCOL_ROCE`不支持多通道模式。
- 调用前必须通过[batch_commit](./batch_commit.md)提交当前批次中的全部任务。存在尚未提交的任务时，调试模式下触发Trap，不返回错误码。
- 可以执行多次`batch_commit`后统一调用一次批量`drain`，接口会等待该batch_handle累计提交任务生成的全部完成记录。
- 若所有已提交任务均配置为`cqe = 0`，本接口会直接返回，但不能据此确认这些任务已经完成。如需通过本接口确认整批任务完成，需要为能够代表整批完成的任务配置`cqe = 1`。默认配置`ASCCOMM_DEFAULT_CFG`的`cqe`为`1`，因此每个任务都会生成完成记录。
- batch_handle使用期间需要独占其关联的通道资源，不能混用普通`drain`。

## 调用示例

drain接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[01_asccomm_urma_write_read_nbi](../../../../../examples/aicore/asccomm/01_asccomm_urma_write_read_nbi/README.md)与[02_asccomm_urma_batch_write](../../../../../examples/aicore/asccomm/02_asccomm_urma_batch_write/README.md)（批量）。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void drain_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];

    // 创建asccomm对象并初始化UB临时工作区
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    asccomm.init(asccomm_local, ASCCOMM_BUF_SIZE);

    // 提交写任务
    asccomm.write_nbi(channel, remote_buffer, local_buffer, BYTES);

    // 阻塞等待通道上已提交任务完成
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

extern "C" __vector__ __global__ __aicore__ void asccomm_batch_drain_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 声明批量UB工作区，批量接口不依赖Init；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    // 创建asccomm对象
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;

    // 创建批量句柄并绑定批量工作区
    auto batch_handle = asccomm.make_batch_handle(
        channel, asccomm_local, BATCH_BUF_SIZE, remote_buffer);

    // 向当前批次依次添加TASK_NUM个写任务，分别写入远端不同偏移
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        asccomm.write_nbi(batch_handle, remote_buffer + i * BYTES, local_buffer, BYTES);
    }
    asccomm.batch_commit(batch_handle);

    // 等待整批任务生成的全部完成记录
    asccomm.drain(batch_handle);
}
```
