# MakeBatchHandle

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

头文件路径：`"hcomm/hcomm_simt.h"`。

创建批量句柄，并绑定批量操作所需的UB工作区。返回的批量句柄类型为`BatchHandle<ChannelHandle>`（即`UbcCtpBatchHandle`），独占使用一个通道，通过该句柄添加的批量任务所访问的远端数据区间和通知地址必须落在该通道的远端注册内存内。单线程批量接口由单个线程创建句柄后直接添加任务；Group协作批量接口由组内线程共同向同一句柄添加任务，任务通过[BatchCommit](./SIMT_BatchCommit.md)提交，通过[Drain](./SIMT_Drain.md)等待完成。

## 函数原型

单线程批量接口：

```cpp
__simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen);
```

Group协作批量接口：

```cpp
template <typename Group>
__simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, const Group& group);
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
| `buffLen` | 输入 | 批量工作区可用长度，单位为字节，至少需要256字节。 |
| `group` | 输入 | SIMT协作组，Group协作批量重载使用。 |

## 返回值说明

返回与`channel`对应的批量句柄。创建失败时返回零值句柄（`context`为`nullptr`），调用方必须在执行任何批量任务添加、[BatchCommit](./SIMT_BatchCommit.md)或[Drain](./SIMT_Drain.md)操作前检查该字段。除有效性检查外，批量句柄应作为不透明句柄使用。该接口不返回状态码，调用方必须保证参数满足约束。

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- `channel`必须有效，并且对应的通信资源已完成初始化。
- 批量工作区由调用方提供，`buff`起始地址需按16字节对齐，`buffLen`不能小于256字节。
- 通过返回句柄添加的每个远端数据区间和通知地址必须落在该通道的远端注册内存内，该约束由调用方保证。
- 每个批次中每个线程恰好添加一个任务。
- Group协作批量接口要求组内所有线程传入相同的`buff`和`buffLen`；句柄生命周期内组成员、lane rank（线程在协作组内的编号）与size保持固定。
- 同一批次内追加的请求必须使用相同的WQE宽度（即占用相同的任务槽位数量，如写通知/原子任务占2个任务槽位，普通写/读任务占1个），混用宽度不受支持，任务添加返回`-1`。
- 一个通道同一时刻只能被一个有效批量句柄独占使用；句柄存活期间，不得在该通道上使用本类的普通`ChannelHandle`提交接口，也不得并发使用其他批量句柄。
- BatchHandle、通道资源和`buff`的生命周期由调用方负责。BatchHandle使用完成前，不得释放或并发复用这些资源。

## 调用示例

MakeBatchHandle接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md)。

单线程批量接口：

```cpp
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtMakeBatchHandleOp(AscendC::ChannelHandle channel)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);
}
```

Group协作批量接口：

```cpp
#include "simt_api/cooperative_groups.h"

// 批量工作区大小：不小于256字节，不随组内线程数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupMakeBatchHandleOp(AscendC::ChannelHandle channel)
{
    // 声明批量UB工作区，组内所有线程传入相同的工作区；起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 取当前线程块作为协作组，组内所有线程以相同实参调用，共同创建批量句柄
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);
}
```
