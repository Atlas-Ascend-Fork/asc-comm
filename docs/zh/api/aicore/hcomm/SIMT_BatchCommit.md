# BatchCommit

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

提交批量句柄当前批次中的全部任务，发布前序批量接口准备好的SQ范围。接口提供单线程批量重载和Group协作批量重载：单线程重载由创建句柄的线程直接提交，当前每批仅支持提交一个任务请求；Group协作重载为协作调用，组内每个线程都必须调用，共同完成批次提交。

## 函数原型

单线程批量接口：

```cpp
__simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle);
```

Group协作批量接口：

```cpp
template <typename Group>
__simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle, const Group& group);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `Group` | SIMT协作组类型，由`simt_api/cooperative_groups.h`提供，仅Group协作重载使用。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄。成功后，当前批次可以重新添加任务。 |
| `group` | 输入 | SIMT协作组，Group协作批量重载使用。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 调用前需要通过批量接口（如[WriteNbi](./SIMT_WriteNbi.md)、[ReadNbi](./SIMT_ReadNbi.md)）至少添加一个任务；若自句柄创建或上次提交以来未添加任何任务，本接口返回`-1`，不会发布空的SQ范围。
- 当前单线程批量接口每批仅支持提交一个任务请求；需要提交多个任务时，逐个添加并逐批提交，或使用Group协作批量重载。
- 若前序添加任务的接口返回`-1`（SQ空间预留失败），不得对该批次调用本接口。
- 批量路径要求每个任务的`config.cqe`为1（默认`URMA_DEFAULT_CFG`/`URMA_INLINE_CFG`已满足）；自定义`config`时`cqe`为0将导致编译报错。
- Group协作重载为协作调用：组内每个线程都必须调用本接口，且每个线程都已向当前批次添加了恰好一个任务。
- 提交成功后，可以复用BatchHandle和工作区添加并提交下一个批次。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通`ChannelHandle`提交流程。
- 批量任务的完成等待通过[Drain](./SIMT_Drain.md)的批量重载完成。
- 同一批次内追加的请求必须使用相同的WQE宽度（即占用相同的任务槽位数量，如写通知/原子任务占2个任务槽位，普通写/读任务占1个），混用宽度不受支持，任务添加返回`-1`。

## 调用示例

BatchCommit接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md)。

单线程批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchCommitOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // 提交第一批任务
    hcomm.WriteNbi(batchHandle, remoteBase, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // 批次提交成功后复用句柄，提交下一批任务
    hcomm.WriteNbi(batchHandle, remoteBase + BYTES, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // 等待累计提交任务生成的全部完成记录
    hcomm.Drain(batchHandle);
}
```

Group协作批量接口：

```cpp
#include "simt_api/cooperative_groups.h"

// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不小于256字节，不随组内线程数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtGroupBatchCommitOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // 声明批量UB工作区，组内所有线程传入相同的工作区；起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 组内所有线程以相同实参创建批量句柄
    auto group = cooperative_groups::this_thread_block();
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE, group);

    // 组内每个线程向当前批次添加一个写任务，按线程rank错开各自的远端地址
    hcomm.WriteNbi(batchHandle, remoteBase + group.thread_rank() * BYTES, localBuffer, BYTES, group);

    // 组内协作提交当前批次中的全部任务
    hcomm.BatchCommit(batchHandle, group);

    // 组内所有线程同步后，由单个线程等待累计提交任务生成的全部完成记录
    group.sync();
    if (group.thread_rank() == 0) {
        hcomm.Drain(batchHandle);
    }
}
```
