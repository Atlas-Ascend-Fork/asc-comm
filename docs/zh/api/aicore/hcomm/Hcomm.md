# Hcomm

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

头文件为：

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm`是AICore侧点对点通信接口模板。普通接口通过`ChannelHandle`提交单个读、写、写通知和原子操作任务，并通过`Commit`和`Drain`控制任务提交与完成等待。UB_CTP路径还提供BatchHandle接口，支持为单个或多个逻辑通道添加多个任务，再通过`BatchCommit`统一提交。

## 函数原型

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UB_CTP>
class Hcomm;
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commProtocol` | 通信协议类型，支持`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UB_CTP`，默认值为`COMM_PROTOCOL_UB_CTP`，选定协议即确定对应通信路径。 |

批量接口仅支持`COMM_PROTOCOL_UB_CTP`协议路径。单通道模式直接使用`MakeBatchHandle`返回的句柄；多通道模式通过`GetHandleRef`选择逻辑通道并添加任务，通过`MakeBatchHandle`返回的多通道批量句柄执行`BatchCommit`和批量`Drain`。

## 返回值说明

返回状态码的接口通常以`0`表示成功、非`0`值表示失败。`Drain`在`COMM_PROTOCOL_UB_CTP`协议路径下还会直接返回完成记录轮询的详细错误码，用于区分轮询超时和完成状态错误。`MakeBatchHandle`直接返回批量句柄，不返回状态码。各接口的具体返回值见对应API文档。

## 约束说明

- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- 普通`ChannelHandle`接口需要先通过`Init`提供临时工作区。BatchHandle调用链不依赖`Init`，其UB工作区由`MakeBatchHandle`提供。
- `WriteValueNbi`、`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`仅支持`COMM_PROTOCOL_UB_CTP`协议路径。
- 当前批量接口仅支持`COMM_PROTOCOL_UB_CTP`协议路径。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通接口或并发使用多个BatchHandle。
- 传入的`ChannelHandle`需要指向与协议匹配的通道实体。
- 若普通接口`commit`模板参数设为`false`，连续积攒的任务槽位不得超过通道的任务提交容量；需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。
- 普通接口批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录（即中间任务的`config.cqe`设为0，最后一次设为1）。

多通道模式的Host侧句柄创建接口见[MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)。该接口提供HCCL通信域和Hcomm Endpoint两种重载。

## 调用示例

[hcomm_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_write_read_nbi/README.md)演示两卡场景下，AIV Kernel通过`COMM_PROTOCOL_UB_CTP`协议路径调用普通`WriteNbi`和`ReadNbi`。该样例不覆盖RoCE路径和BatchHandle流程。
