# make_batch_handle

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

创建批量句柄，并绑定批量操作所需的UB工作区。支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。

调用侧建议使用`auto`接收返回值。单通道模式直接使用返回的句柄添加和提交任务，需要向多个通道提交任务时为每个通道分别创建批量句柄；多通道模式通过[get_handle_ref](./get_handle_ref.md)选择逻辑通道后添加任务，多个逻辑通道共享同一个批量句柄、批量工作区和底层通信资源。批量任务通过[batch_commit](./batch_commit.md)提交，通过[drain](./drain.md)等待完成。

## 函数原型

```cpp
template <typename T>
__aicore__ inline batch_handle<T> make_batch_handle(
    T channel,
    __ubuf__ uint8_t* buff,
    uint32_t buff_len,
    GM_ADDR remote_addr = nullptr,
    GM_ADDR local_addr = nullptr);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 通道句柄类型，支持`ChannelHandle`和`multi_channel_handle`。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 单通道`ChannelHandle`（`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议均支持），或Host侧[make_multi_channel_handle](../../host/asccomm/make_multi_channel_handle.md)创建的`multi_channel_handle`。 |
| `buff` | 输入 | 调用方提供的UB工作区起始地址（`__ubuf__ uint8_t*`），作为批量操作的工作区。 |
| `buff_len` | 输入 | 工作区可用长度，单位为字节。 |
| `remote_addr` | 输入 | 单通道模式下用于选择远端注册内存（MR）。`COMM_PROTOCOL_ROCE`协议下必须传入具体值：该地址须位于Host侧预先注册的远端MR范围内，用于选中该MR；`COMM_PROTOCOL_UB_CTP`协议下传入`nullptr`时选择第一个远端注册内存。多通道模式下忽略。默认值为`nullptr`。 |
| `local_addr` | 输入 | 单通道模式下用于选择本端注册内存（MR）。`COMM_PROTOCOL_ROCE`协议下必须传入具体值：该地址须位于Host侧预先注册的本端MR范围内，用于选中该MR；`COMM_PROTOCOL_UB_CTP`协议下当前版本不使用。默认值为`nullptr`。 |

## 返回值说明

返回与`channel`对应的`batch_handle<T>`。该接口不返回状态码：参数或通道状态不满足约束时（如`channel`无效、`buff`未按32字节对齐、`buff_len`小于64字节、`remote_addr`或`local_addr`不在注册内存范围内）通过调试期断言（Trap）报告错误，调用方必须保证参数满足约束。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- `channel`必须有效，并且对应的通信资源已完成初始化。
- 单通道模式下，`remote_addr`非空时必须位于一个远端注册内存内。后续批量操作访问的远端地址必须属于该注册内存。
- `COMM_PROTOCOL_ROCE`协议下，`remote_addr`与`local_addr`都必须传入具体值，且分别位于Host侧预先注册的远端、本端MR（通信内存）范围内，不满足时调试期断言；后续[write_nbi](./write_nbi.md)、[read_nbi](./read_nbi.md)、[write_with_notify_nbi](./write_with_notify_nbi.md)等批量操作访问的本端与远端地址都必须落在对应MR范围内。`COMM_PROTOCOL_UB_CTP`协议下`local_addr`为保留参数。
- 多通道模式下，通过`get_handle_ref`选择逻辑通道和远端注册内存。
- `buff`起始地址必须按32字节对齐。
- `buff_len`不能小于64字节。不能被64整除的剩余空间不可用。
- 一个批次实际占用的64字节任务槽位数量不能超过`buff_len`折算的批量工作区槽位数，否则对应的批量任务添加接口（[write_nbi](./write_nbi.md)、[read_nbi](./read_nbi.md)、[write_with_notify_nbi](./write_with_notify_nbi.md)等）返回`-1`（`COMM_PROTOCOL_UB_CTP`协议下为接口校验行为，`COMM_PROTOCOL_ROCE`协议下该超限为调试期断言）；同时必须小于通道的任务提交容量，容量不足时`batch_commit`会先等待在途任务完成以腾出空间。任务提交容量即通道的SQ深度（sqDepth），在Host侧创建通道资源时确定。
- batch_handle、通道资源、`multi_channel_handle`和`buff`的生命周期由调用方负责。batch_handle使用完成前，不得释放或并发复用这些资源。
- 使用batch_handle期间必须独占其关联的通道资源，不能交叉调用普通`ChannelHandle`接口，也不能并发使用其他batch_handle。
- batch_handle应作为不透明句柄使用，调用方不应直接访问或修改其成员。
- `COMM_PROTOCOL_ROCE`协议下批量句柄类型为`ubc_batch_handle`，不支持多通道选择；多通道批量仅`COMM_PROTOCOL_UB_CTP`协议支持，`COMM_PROTOCOL_ROCE`不支持多通道模式，[get_handle_ref](./get_handle_ref.md)为直通实现。

## 调用示例

make_batch_handle接口的调用示例如下，完整样例参见[02_asccomm_urma_batch_write](../../../../../examples/aicore/asccomm/02_asccomm_urma_batch_write/README.md)与[08_asccomm_roce_batch_write_read](../../../../../examples/aicore/asccomm/08_asccomm_roce_batch_write_read/README.md)（`COMM_PROTOCOL_ROCE`协议）。

```cpp
// 前置条件：以下符号由调用环境提供
// channel：单通道句柄（ChannelHandle）；multi_channel：Host侧make_multi_channel_handle创建的多通道句柄
// remote_addr / peer_remote_addr：远端注册内存内的地址（GM_ADDR）；channel_idx：逻辑通道索引（uint32_t）
// 批量工作区大小：按64字节任务槽划分，起始地址需按32字节对齐
constexpr uint32_t BATCH_BUF_SIZE = 128U;
alignas(32) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;

// 单通道：创建批量句柄并绑定批量工作区，remote_addr用于选择远端注册内存
auto batch_handle = asccomm.make_batch_handle(
    channel, asccomm_local, BATCH_BUF_SIZE, remote_addr);

// 多通道：创建多通道批量句柄，通过get_handle_ref选择逻辑通道及远端注册内存
auto multi_batch_handle = asccomm.make_batch_handle(
    multi_channel, asccomm_local, BATCH_BUF_SIZE);
auto& peer_batch_handle = asccomm.get_handle_ref(
    multi_batch_handle, channel_idx, peer_remote_addr);

// 多通道模式下，通过`peer_batch_handle`添加批量任务，通过`multi_batch_handle`调用`batch_commit`和批量`drain`。
```
