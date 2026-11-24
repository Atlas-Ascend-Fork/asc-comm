# read_nbi

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

将数据从远端`src`读取到本端`dst`。接口提供普通`channel`重载和批量`batch_handle`重载：普通重载直接向通道添加读任务；批量重载将读任务添加到当前批次，后续由[batch_commit](./batch_commit.md)统一提交。

## 函数原型

普通接口：

```cpp
template <
    bool commit = true,
    pipe_t commit_pipe = PIPE_S,
    pipe_t req_pipe = PIPE_MTE3,
    auto const& config = ASCCOMM_DEFAULT_CFG>
__aicore__ inline int32_t read_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

批量接口：

```cpp
template <
    auto const& config = ASCCOMM_DEFAULT_CFG,
    typename T,
    typename handle_traits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t read_nbi(
    T& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

普通多SGE（Scatter-Gather Element，离散内存段描述）接口：

```cpp
template <
    bool commit = true,
    pipe_t commit_pipe = PIPE_S,
    pipe_t req_pipe = PIPE_MTE3,
    auto const& config = ASCCOMM_DEFAULT_CFG>
__aicore__ inline int32_t read_nbi(
    ChannelHandle channel, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);
```

批量多SGE（Scatter-Gather Element，离散内存段描述）接口：

```cpp
template <
    auto const& config = ASCCOMM_DEFAULT_CFG,
    typename T,
    typename handle_traits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t read_nbi(T& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。批量接口不提供该参数。 |
| `commit_pipe` | 普通接口commit使用的pipe，默认`PIPE_S`。 |
| `req_pipe` | 普通接口请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | WQE控制配置，默认为`ASCCOMM_DEFAULT_CFG`（执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE）；`COMM_PROTOCOL_ROCE`协议下仅`cqe`与`fence`字段生效。 |
| `T` | 批量句柄类型，由`batch_handle`实参推导。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batch_handle` | 输入/输出 | [make_batch_handle](./make_batch_handle.md)创建的单通道批量句柄，或[get_handle_ref](./get_handle_ref.md)返回的batch_handle引用。 |
| `dst` | 输出 | 本端目的GM（Global Memory，全局内存）绝对地址。 |
| `src` | 输入 | 远端源GM绝对地址。 |
| `len` | 输入 | 读取长度，单位为字节。`COMM_PROTOCOL_UB_CTP`协议下不超过256MB（256 * 1024 * 1024字节），`COMM_PROTOCOL_ROCE`协议下小于2GB（2 * 1024 * 1024 * 1024字节）。 |
| `dst_descs` | 输入 | 本端离散内存段描述数组（多SGE接口），每个元素包含`addr`（段起始GM地址）与`len`（段长度，单位为字节）。 |
| `dst_num` | 输入 | 内存段数量（多SGE接口）。`COMM_PROTOCOL_UB_CTP`协议下取值范围为1-6，`COMM_PROTOCOL_ROCE`协议下为1-24。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或读任务成功添加到当前批次。 |

接口约束不满足时（如批量工作区剩余空间不足）通过调试期断言（Trap）报告错误，不返回错误码。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- `len`为传输长度，单位为字节：`COMM_PROTOCOL_UB_CTP`协议下不超过256MB（256 * 1024 * 1024字节）；`COMM_PROTOCOL_ROCE`协议下小于2GB（2 * 1024 * 1024 * 1024字节）。

**普通接口**

- 调用前通信通道需已完成初始化，并通过`init`提供临时工作区。
- `src`需要落在通道注册的远端注册内存范围内，`dst`为本端目标地址。
- 单个`read_nbi`任务占用1个任务槽位（64字节）；`commit`模板参数设为`false`时，积攒的延迟提交任务占用的槽位数之和不得超过`sqDepth`，需在SQ耗尽前通过`commit`或自动commit提交积攒的任务，否则后续任务提交将因SQ溢出而失败。
- 延迟提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录（即中间任务的`config.cqe`设为0，最后一次设为1）。

**批量接口**

- 每个批量读任务占用1个任务槽位（64字节）批量工作区。
- 对批量句柄，`[src, src + len)`必须属于其选中的远端注册内存。
- 批量工作区剩余空间不足时，调试模式下触发Trap，不返回错误码。
- 添加完成后需要调用[batch_commit](./batch_commit.md)。多通道模式通过[get_handle_ref](./get_handle_ref.md)返回的句柄引用添加任务，并通过[make_batch_handle](./make_batch_handle.md)返回的多通道批量句柄提交。使用期间需要独占关联的通道资源。
- 多通道批量仅`COMM_PROTOCOL_UB_CTP`协议支持，`COMM_PROTOCOL_ROCE`不支持多通道模式。
- `COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议均支持多SGE接口。多SGE接口中，从远端`src`读取的数据写入`dst_descs`给出的各离散本端内存段，总长度为各段`len`之和；`dst_num`取值范围：`COMM_PROTOCOL_UB_CTP`协议下为1-6，`COMM_PROTOCOL_ROCE`协议下为1-24，调试模式下超出范围会触发Trap；`COMM_PROTOCOL_ROCE`协议下各段`len`及累加总长度均须小于2GB（2 * 1024 * 1024 * 1024字节）。

## 调用示例

read_nbi接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[01_asccomm_urma_write_read_nbi](../../../../../examples/aicore/asccomm/01_asccomm_urma_write_read_nbi/README.md)。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void read_nbi_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];

    // 创建asccomm对象并初始化UB临时工作区
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    asccomm.init(asccomm_local, ASCCOMM_BUF_SIZE);

    // 远端remote_buffer数据读取到本端local_buffer
    asccomm.read_nbi(channel, local_buffer, remote_buffer, BYTES);

    // 阻塞等待读任务完成
    asccomm.drain(channel);
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

extern "C" __vector__ __global__ __aicore__ void asccomm_batch_read_nbi_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 声明批量UB工作区，批量接口不依赖Init；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    // 创建asccomm对象
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;

    // 创建单通道批量句柄并绑定批量工作区，remote_buffer用于选择远端注册内存
    auto batch_handle = asccomm.make_batch_handle(
        channel, asccomm_local, BATCH_BUF_SIZE, remote_buffer);

    // 向当前批次依次添加TASK_NUM个读任务，分别读取远端不同偏移到本端不同偏移；任务先积攒在批量工作区，尚未提交
    for (uint32_t i = 0U; i < TASK_NUM; ++i) {
        asccomm.read_nbi(batch_handle, local_buffer + i * BYTES, remote_buffer + i * BYTES, BYTES);
    }

    // 一次提交当前批次积攒的全部任务
    asccomm.batch_commit(batch_handle);

    // 阻塞等待整批任务生成的全部完成记录
    asccomm.drain(batch_handle);
}
```
