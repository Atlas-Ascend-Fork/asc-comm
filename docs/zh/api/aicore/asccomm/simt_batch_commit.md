# batch_commit

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

提交批量句柄当前批次中的全部任务，发布前序批量接口准备好的SQ范围。接口提供单线程批量重载和Group协作批量重载：单线程重载由创建句柄的线程直接提交，当前每批仅支持提交一个任务请求；Group协作重载为协作调用，组内每个线程都必须调用，可一次提交已成功添加的一轮或多轮任务。

## 函数原型

单线程批量接口：

```cpp
__simt_callee__ inline int32_t batch_commit(ubc_ctp_batch_handle& batch_handle);
```

Group协作批量接口：

```cpp
template <typename coop_group>
__simt_callee__ inline int32_t batch_commit(ubc_ctp_batch_handle& batch_handle, const coop_group& group);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `Group` | SIMT协作组类型，由`simt_api/cooperative_groups.h`提供，仅Group协作重载使用。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `batch_handle` | 输入/输出 | [make_batch_handle](./simt_make_batch_handle.md)创建的批量句柄。成功后，当前批次可以重新添加任务。 |
| `group` | 输入 | SIMT协作组，Group协作批量重载使用。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。例如，Group协作批量模式下，当前批次没有成功添加的任务。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 调用前需要通过批量接口（如[write_nbi](./simt_write_nbi.md)、[read_nbi](./simt_read_nbi.md)）至少添加一个任务。
- 当前单线程批量接口每批仅支持提交一个任务请求；需要提交多个任务时，逐个添加并逐批提交，或使用Group协作批量重载。
- 单线程批量模式下，若前序添加任务的接口返回`-1`（SQ空间预留失败），不得对该批次调用本接口。
- Group模式下，追加失败的轮次不进入批次，此前成功追加的轮次仍可提交。因队列空间不足需要重试时，先提交已成功追加的轮次（如有），再由组内一个线程调用[drain](./simt_drain.md)。组内同步后根据同一等待结果决定是否重试，`drain`成功才重试。
- 批量路径要求每个任务的`config.cqe`为1（默认`ASCCOMM_DEFAULT_CFG`/`ASCCOMM_INLINE_CFG`已满足）；自定义`config`时`cqe`为0将导致编译报错。
- Group协作重载为协作调用：组内每个线程都必须使用创建句柄时的协作组调用本接口。
- 提交成功后，可以复用batch_handle和工作区添加并提交下一个批次。
- 使用batch_handle期间必须独占其关联的通道资源，不能混用普通`ChannelHandle`提交流程。
- 批量任务的完成等待通过[drain](./simt_drain.md)的批量重载完成。
- 同一批次内的请求须使用相同的WQE宽度（如写通知和原子任务占2个任务槽位，普通写和读任务占1个）。Group模式下，若下一轮需要不同宽度，须先提交当前批次，再复用同一句柄追加下一轮。

### Group模式容量规划

设组内有`G`个线程。当前实现中，写、读或立即数写每轮需要`G+1`个SQ基本块，其中最后一个请求占两个。写通知或原子操作每轮需要`2G`个SQ基本块。两种宽度每轮均需要`G`个CQ槽位。

如果当前没有待提交的请求，且`drain`已成功回收通道上的所有已提交请求，继续调用`drain`也不会释放更多槽位。若通道容量仍小于一轮的需求，请用符合约束的更小协作组重新创建批量句柄。

## 调用示例

`batch_commit`接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[06_simt_asccomm_urma_batch_perf](../../../../../examples/aicore/asccomm/06_simt_asccomm_urma_batch_perf/README.md)。

单线程批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void simt_batch_commit_op(
    ChannelHandle channel, __gm__ uint8_t* local_buffer, __gm__ uint8_t* remote_base)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batch_handle = asccomm.make_batch_handle(channel, asccomm_local, BATCH_BUF_SIZE);

    // 提交第一批任务
    asccomm.write_nbi(batch_handle, remote_base, local_buffer, BYTES);
    asccomm.batch_commit(batch_handle);

    // 批次提交成功后复用句柄，提交下一批任务
    asccomm.write_nbi(batch_handle, remote_base + BYTES, local_buffer, BYTES);
    asccomm.batch_commit(batch_handle);

    // 等待累计提交任务生成的全部完成记录
    asccomm.drain(batch_handle);
}
```

Group协作批量接口：

```cpp
#include "simt_api/cooperative_groups.h"

// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不小于256字节，不随组内线程数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void simt_group_batch_commit_op(
    ChannelHandle channel, __gm__ uint8_t* local_buffer, __gm__ uint8_t* remote_base)
{
    // 声明批量UB工作区，组内所有线程传入相同的工作区；起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 组内所有线程以相同实参创建批量句柄
    auto group = cooperative_groups::this_thread_block();
    auto batch_handle = asccomm.make_batch_handle(channel, asccomm_local, BATCH_BUF_SIZE, group);

    // 组内每个线程向当前批次添加一个写任务，按线程rank错开各自的远端地址
    asccomm.write_nbi(batch_handle, remote_base + group.thread_rank() * BYTES, local_buffer, BYTES, group);

    // 组内协作提交当前批次中的全部任务
    asccomm.batch_commit(batch_handle, group);

    // 组内所有线程同步后，由单个线程等待累计提交任务生成的全部完成记录
    group.sync();
    if (group.thread_rank() == 0) {
        asccomm.drain(batch_handle);
    }
}
```
