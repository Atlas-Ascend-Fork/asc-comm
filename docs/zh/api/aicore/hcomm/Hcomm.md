# Hcomm

## 功能说明

头文件为：

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm`是AICore侧点对点通信接口模板。普通接口通过`ChannelHandle`提交单个读、写、写通知和原子操作任务，并通过`Commit`和`Drain`控制任务提交与完成等待。UBC_CTP路径还提供BatchHandle接口，支持为单个或多个逻辑通道添加多个任务，再通过`BatchCommit`统一提交。

## 模板参数

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| 参数 | 说明 |
| --- | --- |
| `commProtocol` | 通信协议类型，支持`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UBC_CTP`，默认值为`COMM_PROTOCOL_UBC_CTP`。 |

批量接口仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。单通道模式直接使用`MakeBatchHandle`返回的句柄；多通道模式通过`GetHandleRef`选择逻辑通道并添加任务，通过`MakeBatchHandle`返回的多通道批量句柄执行`BatchCommit`和批量`Drain`。

## 协议能力

| 接口 | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | 支持，用于普通接口的UB临时工作区。 | 支持，用于普通接口的URMA临时工作区。 |
| `Lock` | 不支持。 | 支持，仅限Ascend 950平台的AIV。 |
| `Unlock` | 不支持。 | 支持，仅限Ascend 950平台的AIV。 |
| 普通`ReadNbi` | 支持。 | 支持。 |
| 普通`WriteNbi` | 支持。 | 支持。 |
| 普通`WriteValueNbi` | 不支持，调用会返回失败。 | 支持。 |
| 普通`WriteWithNotifyNbi` | 不支持，调用会返回失败。 | 支持。 |
| `AtomicFAA` | 不支持。 | 支持。 |
| `AtomicCAS` | 不支持。 | 支持。 |
| 普通`Commit` | 支持。 | 支持。 |
| 普通`Drain` | 支持。 | 支持。 |
| `MakeBatchHandle` | 不支持。 | Ascend 950支持。 |
| `GetHandleRef` | 不支持。 | Ascend 950多通道批量路径支持。 |
| 批量`ReadNbi` | 不支持。 | Ascend 950支持。 |
| 批量`WriteNbi` | 不支持。 | Ascend 950支持。 |
| 批量`WriteWithNotifyNbi` | 不支持。 | Ascend 950支持。 |
| `BatchCommit` | 不支持。 | Ascend 950支持。 |
| 批量`Drain` | 不支持。 | Ascend 950支持。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [Init](./Init.md) | 初始化普通Hcomm接口使用的临时工作区。 |
| [MakeBatchHandle](./MakeBatchHandle.md) | 创建批量句柄并绑定批量操作所需的UB工作区。 |
| [GetHandleRef](./GetHandleRef.md) | 从多通道批量句柄中选择逻辑通道。 |
| [Lock](./Lock.md) | 获取通信通道的跨AI Core锁。 |
| [Unlock](./Unlock.md) | 释放通信通道的跨AI Core锁。 |
| [ReadNbi](./ReadNbi.md) | 通过普通通道提交读任务，或向BatchHandle添加读任务。 |
| [WriteNbi](./WriteNbi.md) | 通过普通通道提交写任务，或向BatchHandle添加写任务。 |
| [WriteValueNbi](./WriteValueNbi.md) | 通过普通通道提交立即数写任务，将`value`作为内联数据写入远端。 |
| [WriteWithNotifyNbi](./WriteWithNotifyNbi.md) | 通过普通通道提交写通知任务，或向BatchHandle添加写通知任务。 |
| [AtomicFAA](./AtomicFAA.md) | 提交Fetch-and-add原子操作任务。 |
| [AtomicCAS](./AtomicCAS.md) | 提交Compare-and-swap原子操作任务。 |
| [Commit](./Commit.md) | 显式提交普通通道上的待执行任务。 |
| [BatchCommit](./BatchCommit.md) | 提交BatchHandle中的全部任务。 |
| [Drain](./Drain.md) | 等待普通通道或BatchHandle已提交的任务完成。 |

## 返回值

返回状态码的接口通常以`0`表示成功、非`0`值表示失败。`Drain`在`COMM_PROTOCOL_UBC_CTP`路径下还会直接返回完成记录轮询的详细错误码，用于区分轮询超时和完成状态错误。`MakeBatchHandle`直接返回批量句柄，不返回状态码。各接口的具体返回值见对应API文档。

## 使用约束

- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- 普通`ChannelHandle`接口需要先通过`Init`提供临时工作区。BatchHandle调用链不依赖`Init`，其UB工作区由`MakeBatchHandle`提供。
- `WriteValueNbi`、`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`仅支持`COMM_PROTOCOL_UBC_CTP`路径。
- 当前批量接口仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通接口或并发使用多个BatchHandle。
- 传入的`ChannelHandle`需要指向与协议匹配的通道实体。
- 若普通接口`commit`模板参数设为`false`，连续积攒的任务槽位不得超过通道的任务提交容量；需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。
- 普通接口批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录（即中间任务的`config.cqe`设为0，最后一次设为1）。

多通道模式的Host侧句柄创建接口见[MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)。该接口提供HCCL通信域和Hcomm Endpoint两种重载。

## 相关样例

[hcomm_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_write_read_nbi/README.md)演示两卡场景下，AIV Kernel通过`COMM_PROTOCOL_UBC_CTP`路径调用普通`WriteNbi`和`ReadNbi`。该样例不覆盖RoCE路径和BatchHandle流程。
