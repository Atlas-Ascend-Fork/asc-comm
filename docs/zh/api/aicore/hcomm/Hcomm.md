# Hcomm

## 功能说明

头文件为：

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm`是AICore侧点对点通信接口模板。普通接口通过`ChannelHandle`提交单个读、写、写通知和原子操作任务，并通过`Commit`和`Drain`控制任务提交与完成等待。UBC_CTP路径还提供BatchHandle接口，既支持单通道批量提交，也支持通过MultiChannelHandle`在共享Jetty上为多个逻辑通道准备WQE，再通过`BatchCommit`一次提交。

## 模板参数

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| 参数 | 说明 |
| --- | --- |
| `commProtocol` | 通信协议类型，支持`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UBC_CTP`，默认值为`COMM_PROTOCOL_UBC_CTP`。 |

`MakeBatchHandle`通过编译期traits推导返回类型：`ChannelHandle`映射为`UbcCtpBatchHandle`，`MultiChannelHandle`映射为`UbcCtpMultiBatchHandle`。多通道模式通过`GetHandleRef`返回的内层`UbcCtpBatchHandle`引用执行批量读写，并通过外层`UbcCtpMultiBatchHandle`执行`BatchCommit`和批量`Drain`。批量接口仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

## 协议能力

| 接口 | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | 支持，用于普通接口的UB临时工作区。 | 支持，用于普通接口的URMA临时工作区。 |
| 普通`ReadNbi` | 支持。 | 支持。 |
| 普通`WriteNbi` | 支持。 | 支持。 |
| 普通`WriteWithNotifyNbi` | 不支持，调用会返回失败。 | 支持。 |
| `AtomicFAA` | 不支持。 | 支持。 |
| `AtomicCAS` | 不支持。 | 支持。 |
| 普通`Commit` | 支持。 | 支持。 |
| 普通`Drain` | 支持。 | 支持。 |
| `MakeBatchHandle` | 不支持。 | Ascend 950支持。 |
| `GetHandleRef` | 不支持。 | Ascend 950共享Jetty批量路径支持。 |
| 批量`ReadNbi` | 不支持。 | Ascend 950支持。 |
| 批量`WriteNbi` | 不支持。 | Ascend 950支持。 |
| 批量`WriteWithNotifyNbi` | 不支持。 | Ascend 950支持。 |
| `BatchCommit` | 不支持。 | Ascend 950支持。 |
| 批量`Drain` | 不支持。 | Ascend 950支持。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [Init](./Init.md) | 初始化普通Hcomm接口使用的临时工作区。 |
| [MakeBatchHandle](./MakeBatchHandle.md) | 创建批量句柄并绑定批量WQE的UB缓冲区。 |
| [GetHandleRef](./GetHandleRef.md) | 从共享Jetty多通道批量句柄中选择逻辑通道。 |
| [ReadNbi](./ReadNbi.md) | 通过普通通道提交读任务，或在BatchHandle中准备读WQE。 |
| [WriteNbi](./WriteNbi.md) | 通过普通通道提交写任务，或在BatchHandle中准备写WQE。 |
| [WriteWithNotifyNbi](./WriteWithNotifyNbi.md) | 通过普通通道提交写通知任务，或在BatchHandle中准备写通知WQE。 |
| [AtomicFAA](./AtomicFAA.md) | 提交Fetch-and-add原子操作任务。 |
| [AtomicCAS](./AtomicCAS.md) | 提交Compare-and-swap原子操作任务。 |
| [Commit](./Commit.md) | 显式提交普通通道上的待执行任务。 |
| [BatchCommit](./BatchCommit.md) | 提交BatchHandle中已准备的全部WQE。 |
| [Drain](./Drain.md) | 等待普通通道或BatchHandle已提交的任务完成。 |

## 返回值

返回状态码的接口通常以`0`表示成功、`-1`表示失败；`Drain`还可能返回底层CQ轮询错误码。`MakeBatchHandle`直接返回协议相关的批量句柄，不返回状态码。各接口的具体返回值见对应API文档。

## 使用约束

- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- 普通`ChannelHandle`接口需要先通过`Init`提供临时工作区。BatchHandle调用链不依赖`Init`，其UB工作区由`MakeBatchHandle`提供。
- `WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`的普通重载仅支持`COMM_PROTOCOL_UBC_CTP`路径。
- 当前批量接口仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。
- 普通接口和批量接口使用不同的队列状态管理方式。BatchHandle缓存创建时的SQ/CQ上下文和队列计数，使用期间必须独占对应单通道或共享Jetty，不能混用普通接口或并发使用多个BatchHandle。
- 传入的`ChannelHandle`需要指向与协议匹配的通道实体。
- 若普通接口`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 普通接口批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

共享Jetty的Host侧句柄创建接口见[MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)。

## 相关样例

[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README.md)演示两卡场景下，AIV Kernel通过`COMM_PROTOCOL_UBC_CTP`路径调用普通`WriteNbi`和`ReadNbi`。该样例不覆盖RoCE路径和BatchHandle流程。
