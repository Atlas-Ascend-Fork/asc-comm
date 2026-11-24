# write_with_notify_nbi

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

头文件路径：`"asccomm/asccomm_simt.h"`。

将数据从本端`src`写入远端`dst`，并携带远端通知地址和通知值。接口提供普通`channel`重载、批量`batch_handle`重载和批量Group协作重载：普通重载直接向通道添加写通知任务；批量重载将写通知任务添加到当前批次，后续由[batch_commit](./simt_batch_commit.md)统一提交；Group协作重载由组内多个SIMT线程协作添加批量任务。

## 函数原型

普通接口：

```cpp
template <bool commit = true, auto const& config = ASCCOMM_DEFAULT_CFG>
__simt_callee__ inline int32_t write_with_notify_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val);
```

批量接口：

```cpp
template <auto const& config = ASCCOMM_DEFAULT_CFG>
__simt_callee__ inline int32_t write_with_notify_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val);
```

批量Group协作接口：

```cpp
template <auto const& config = ASCCOMM_DEFAULT_CFG, typename coop_group>
__simt_callee__ inline int32_t write_with_notify_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val, const coop_group& group);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。`false`表示延迟提交，后续由`commit`为`true`的调用统一触发。 |
| `config` | WQE控制配置，默认为`ASCCOMM_DEFAULT_CFG`（执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE）。 |
| `Group` | SIMT协作组类型，由批量Group协作重载使用。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batch_handle` | 输入/输出 | [make_batch_handle](./simt_make_batch_handle.md)创建的批量句柄。 |
| `dst` | 输出 | 远端目的GM（Global Memory，全局内存）地址。 |
| `src` | 输入 | 本端源GM地址。 |
| `len` | 输入 | 写入长度，单位为字节。必须不超过256MB（256 * 1024 * 1024字节）。 |
| `notify_addr` | 输入 | 远端通知GM地址。 |
| `notify_val` | 输入 | 远端通知值。 |
| `group` | 输入 | SIMT协作组，批量Group协作重载使用。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或写通知任务成功添加到当前批次。 |
| `-1` | 操作失败。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- `dst`访问区间和`notify_addr`需要落在通道注册的远端注册内存范围内。
- `len`为传输长度，单位为字节，必须不超过256MB（256 * 1024 * 1024字节）。

**普通接口**

- 调用前通信通道资源需已完成初始化。
- 单个普通写通知任务无论提交方式均占用2个任务槽位（128字节）；`commit`模板参数设为`false`时，积攒的延迟提交任务占用的槽位数与发布该批的`commit`为`true`任务占用的槽位数之和不得超过`sqDepth`，需在SQ耗尽前由`commit`为`true`的任务提交积攒的任务，否则后续任务提交将因SQ溢出而失败。
- 延迟提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录（即中间任务的`config.cqe`设为0，最后一次设为1）。

**批量接口**

- 添加完成后需要调用[batch_commit](./simt_batch_commit.md)提交当前批次。
- 批量任务访问的`dst`地址区间和`notify_addr`必须落在批量句柄所属通道的远端注册内存内，该约束由调用方保证。
- 同一批次内追加的请求必须使用相同的WQE宽度（即占用相同的任务槽位数量，如写通知/原子任务占2个任务槽位，普通写/读任务占1个），混用宽度不受支持。
- 批量重载不提供`commit`参数，固定按延迟语义添加任务，每个批量写通知任务占用2个任务槽位（128字节）。

## 调用示例

write_with_notify_nbi接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，普通接口完整样例参见[04_simt_asccomm_urma](../../../../../examples/aicore/asccomm/04_simt_asccomm_urma/README.md)，批量接口完整样例参见[06_simt_asccomm_urma_batch_perf](../../../../../examples/aicore/asccomm/06_simt_asccomm_urma_batch_perf/README.md)。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 写任务完成后写入远端通知地址的通知值
constexpr uint64_t NOTIFY_VAL = 1U;

__simt_callee__ void simt_write_notify_op(
    ChannelHandle channel, __gm__ uint8_t* local_buffer, __gm__ uint8_t* remote_buffer,
    __gm__ uint8_t* notify_addr)
{
    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 本端local_buffer数据写入远端remote_buffer，任务完成后向远端notify_addr写入通知值
    asccomm.write_with_notify_nbi(channel, remote_buffer, local_buffer, BYTES, notify_addr, NOTIFY_VAL);

    // 阻塞等待写通知任务完成
    asccomm.drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 写任务完成后写入远端通知地址的通知值
constexpr uint64_t NOTIFY_VAL = 1U;
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void simt_batch_write_notify_op(
    ChannelHandle channel, __gm__ uint8_t* local_buffer, __gm__ uint8_t* remote_base,
    __gm__ uint8_t* notify_addr)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batch_handle = asccomm.make_batch_handle(channel, asccomm_local, BATCH_BUF_SIZE);

    // 添加当前批次任务并提交
    asccomm.write_with_notify_nbi(batch_handle, remote_base, local_buffer, BYTES, notify_addr, NOTIFY_VAL);
    asccomm.batch_commit(batch_handle);

    // 复用批量句柄添加并提交下一批任务
    asccomm.write_with_notify_nbi(batch_handle, remote_base + BYTES, local_buffer, BYTES, notify_addr, NOTIFY_VAL);
    asccomm.batch_commit(batch_handle);

    // 等待累计提交任务生成的全部完成记录
    asccomm.drain(batch_handle);
}
```

批量Group协作接口：

```cpp
#include "simt_api/cooperative_groups.h"

// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 写任务完成后写入远端通知地址的通知值
constexpr uint64_t NOTIFY_VAL = 1U;
// 批量工作区大小：不小于256字节，不随组内线程数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void simt_group_batch_write_notify_op(
    ChannelHandle channel, __gm__ uint8_t* local_buffer, __gm__ uint8_t* remote_base,
    __gm__ uint8_t* notify_addr)
{
    // 声明批量UB工作区，组内所有线程传入相同的工作区；起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 组内所有线程以相同实参创建批量句柄
    auto group = cooperative_groups::this_thread_block();
    auto batch_handle = asccomm.make_batch_handle(channel, asccomm_local, BATCH_BUF_SIZE, group);

    // 组内每个线程向当前批次添加一个写通知任务，按线程rank错开各自的远端地址
    asccomm.write_with_notify_nbi(
        batch_handle, remote_base + group.thread_rank() * BYTES, local_buffer, BYTES, notify_addr, NOTIFY_VAL, group);

    // 组内协作提交当前批次中的全部任务
    asccomm.batch_commit(batch_handle, group);

    // 组内所有线程同步后，由单个线程等待累计提交任务生成的全部完成记录
    group.sync();
    if (group.thread_rank() == 0) {
        asccomm.drain(batch_handle);
    }
}
```
