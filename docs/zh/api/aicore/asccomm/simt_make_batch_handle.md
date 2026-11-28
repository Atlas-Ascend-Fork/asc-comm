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

头文件路径：`"asccomm/asccomm_simt.h"`。

创建批量句柄，并绑定批量操作所需的UB工作区。返回的批量句柄类型为`batch_handle<ChannelHandle>`（即`ubc_ctp_batch_handle`），独占使用一个通道，通过该句柄添加的批量任务所访问的远端数据区间和通知地址必须落在该通道的远端注册内存内。单线程批量接口由单个线程创建句柄后直接添加任务；Group协作批量接口由组内线程共同向同一句柄添加任务，任务通过[batch_commit](./simt_batch_commit.md)提交，通过[drain](./simt_drain.md)等待完成。

## 函数原型

单线程批量接口：

```cpp
__simt_callee__ inline batch_handle<ChannelHandle> make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len);
```

Group协作批量接口：

```cpp
template <typename coop_group>
__simt_callee__ inline batch_handle<ChannelHandle> make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, const coop_group& group);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `Group` | SIMT协作组类型，由`simt_api/cooperative_groups.h`提供（如`cooperative_groups::this_thread_block()`构造的线程块组），仅Group协作批量重载使用。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄，由返回的批量句柄独占使用。 |
| `buff` | 输入 | 调用方提供的UB批量工作区，起始地址为`__ubuf__`类型指针。 |
| `buff_len` | 输入 | 批量工作区可用长度，单位为字节，至少需要256字节。 |
| `group` | 输入 | SIMT协作组，Group协作批量重载使用。 |

## 返回值说明

返回与`channel`对应的批量句柄。创建失败时返回零值句柄（`context`为`nullptr`），调用方必须在执行任何批量任务添加、[batch_commit](./simt_batch_commit.md)或[drain](./simt_drain.md)操作前检查该字段。除有效性检查外，批量句柄应作为不透明句柄使用。该接口不返回状态码，调用方必须保证参数满足约束。

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- `channel`必须有效，并且对应的通信资源已完成初始化。
- 批量工作区由调用方提供，`buff`起始地址需按16字节对齐，`buff_len`不能小于256字节。
- 通过返回句柄添加的每个远端数据区间和通知地址必须落在该通道的远端注册内存内，该约束由调用方保证。
- 单线程批量接口每批仅支持添加一个任务。
- Group模式下，每轮所有组成员执行同一个带`group`参数的通信调用。例如，一轮写操作中，所有组成员都调用`write_nbi(batch_handle, dst, src, len, group)`。各线程可传入各自的地址和长度。若显式指定`config`或数据类型模板参数，组内须一致。一个批次可连续添加多轮，各轮须使用相同的WQE宽度。
- 创建Group批量句柄时，组内所有成员必须传入相同的`channel`、`buff`和`buff_len`。句柄生命周期内使用同一协作组，组成员、rank及大小保持固定。
- 协作组至少包含16个线程，并按连续的硬件线程编号排列。组内rank为0的线程须位于硬件warp的lane 0或lane 16。
- 一个通道同一时刻只能被一个有效批量句柄独占使用；句柄存活期间，不得在该通道上使用本类的普通`ChannelHandle`提交接口，也不得并发使用其他批量句柄。
- 并行使用的Group批量句柄须使用互不重叠的UB工作区。
- batch_handle、通道资源和`buff`的生命周期由调用方负责。batch_handle使用完成前，不得释放或并发复用这些资源。

## 调用示例

make_batch_handle接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[06_simt_asccomm_urma_batch_perf](../../../../../examples/aicore/asccomm/06_simt_asccomm_urma_batch_perf/README.md)。

单线程批量接口：

```cpp
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void simt_make_batch_handle_op(ChannelHandle channel)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batch_handle = asccomm.make_batch_handle(channel, asccomm_local, BATCH_BUF_SIZE);
}
```

Group协作批量接口：

```cpp
#include "simt_api/cooperative_groups.h"

// 批量工作区大小：不小于256字节，不随组内线程数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void simt_group_make_batch_handle_op(ChannelHandle channel)
{
    // 声明批量UB工作区，组内所有线程传入相同的工作区；起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 取当前线程块作为协作组，组内所有线程以相同实参调用，共同创建批量句柄
    auto group = cooperative_groups::this_thread_block();
    auto batch_handle = asccomm.make_batch_handle(channel, asccomm_local, BATCH_BUF_SIZE, group);
}
```
