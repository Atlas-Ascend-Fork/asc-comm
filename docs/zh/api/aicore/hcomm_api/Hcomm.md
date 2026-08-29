# Hcomm

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径为：`"comm_api/aicore/hcomm/hcomm.h"`。

`AscendC::Hcomm`是AI Core侧点对点通信接口模板。普通接口通过`ChannelHandle`提交单个读、写、写通知和原子操作任务，并通过`Commit`和`Drain`控制任务提交与完成等待。UBC_CTP路径还提供BatchHandle接口，既支持单通道批量提交，也支持通过MultiChannelHandle`在共享Jetty上为多个逻辑通道准备WQE，再通过`BatchCommit`一次提交。

## 函数原型

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

## 参数说明

**表1**  模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commProtocol` | 通信协议类型，支持RoCE协议`COMM_PROTOCOL_ROCE`和UB_CTP协议`COMM_PROTOCOL_UBC_CTP`，默认值为`COMM_PROTOCOL_UBC_CTP`。 |

`MakeBatchHandle`通过编译期traits推导返回类型：`ChannelHandle`映射为`UbcCtpBatchHandle`，`MultiChannelHandle`映射为`UbcCtpMultiBatchHandle`。多通道模式通过`GetHandleRef`返回的内层`UbcCtpBatchHandle`引用执行批量读写，并通过外层`UbcCtpMultiBatchHandle`执行`BatchCommit`和批量`Drain`。批量接口仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

## 返回值说明

返回状态码的接口通常以`0`表示成功、`-1`表示失败；`Drain`还可能返回底层CQ轮询错误码。`MakeBatchHandle`直接返回协议相关的批量句柄，不返回状态码。各接口的具体返回值见对应API文档。

## 约束说明

- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- 普通`ChannelHandle`接口需要先通过`Init`提供临时工作区。BatchHandle调用链不依赖`Init`，其UB工作区由`MakeBatchHandle`提供。
- `WriteValueNbi`、`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`仅支持`COMM_PROTOCOL_UBC_CTP`路径。
- 当前批量接口仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。
- 普通接口和批量接口使用不同的队列状态管理方式。BatchHandle缓存创建时的SQ/CQ上下文和队列计数，使用期间必须独占对应单通道或共享Jetty，不能混用普通接口或并发使用多个BatchHandle。
- 传入的`ChannelHandle`需要指向与协议匹配的通道实体。
- 若普通接口`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 普通接口批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

共享Jetty的Host侧句柄创建接口见[MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md)。

## 调用示例

Hcomm普通接口的调用示例如下。

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
pipe->InitBuffer(hcommBuf, HCOMM_WORKSPACE_SIZE);
auto hcommTensor = hcommBuf.Get<uint8_t>();
hcomm.Init(hcommTensor, HCOMM_WORKSPACE_SIZE);
hcomm.WriteNbi(channel, writeDst, writeSrc, DATA_SIZE);
hcomm.Drain(channel);
```
