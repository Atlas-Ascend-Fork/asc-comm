# HcommWriteReduceOnThread

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
<!-- npu="310p" id4 -->
- Atlas 推理系列产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->

## 功能说明

头文件：[`#include "hcomm_primitives.h"`](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_primitives.h)。

向channel上的指定内存写数据，将源地址`src`中长度为$count \times \operatorname{sizeof}(dataType)$的数据与目的地址`dst`中相同长度的数据进行reduceOp操作，并将结果写入目的地址`dst`。接口调用方为源地址`src`所在节点。

## 函数原型

```cpp
int32_t HcommWriteReduceOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | -------------------------------------- |
| thread | 输入 | 通信线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>ThreadHandle类型定义可参考[ThreadHandle](../公共类型/ThreadHandle.md)。 |
| channel | 输入 | 通信通道句柄，取值为通过[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口获取到的有效通信通道句柄。<br>ChannelHandle类型定义可参考[ChannelHandle](../公共类型/ChannelHandle.md)。 |
| dst | 输出 | 目的内存地址，使用[HcclGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclGetHcclBuffer.md)、[HcclChannelGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetHcclBuffer.md)获取到的内存。 |
| src | 输入 | 源内存地址，使用[HcclGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclGetHcclBuffer.md)、[HcclChannelGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetHcclBuffer.md)获取到的内存。 |
| count | 输入 | 元素个数。有效范围由源内存和目的内存的可用长度决定，$count \times \operatorname{sizeof}(dataType)$不得超过两者中较小的可用长度。 |
| dataType | 输入 | 数据类型。支持的数据类型请参见[数据类型](#section_hcommwritereduceonthread_datatype)。<br>[HcommDataType](../公共类型/HcommDataType.md)类型的定义请参见对应公共类型文档。 |
| reduceOp | 输入 | 归约操作类型，取值范围：sum、max、min。<br>[HcommReduceOp](../公共类型/HcommReduceOp.md)类型的定义请参见对应公共类型文档。 |

### 数据类型<a id="section_hcommwritereduceonthread_datatype"></a>

<!-- npu="950" id6 -->
针对Ascend 950PR/Ascend 950DT，`dataType`参数支持的`HcommDataType`枚举值见表2。

表2 数据类型

| `HcommDataType`枚举值 | 数据类型 |
| --- | --- |
| `HCOMM_DATA_TYPE_INT8` | int8 |
| `HCOMM_DATA_TYPE_INT16` | int16 |
| `HCOMM_DATA_TYPE_INT32` | int32 |
| `HCOMM_DATA_TYPE_UINT8` | uint8 |
| `HCOMM_DATA_TYPE_UINT16` | uint16 |
| `HCOMM_DATA_TYPE_UINT32` | uint32 |
| `HCOMM_DATA_TYPE_FP16` | float16 |
| `HCOMM_DATA_TYPE_FP32` | float32 |
| `HCOMM_DATA_TYPE_BFP16` | bfp16 |
<!-- end id6 -->
<!-- npu="A3" id7 -->
针对Atlas A3 训练系列产品/Atlas A3 推理系列产品，`dataType`参数支持的`HcommDataType`枚举值见表2。

表2 数据类型

| `HcommDataType`枚举值 | 数据类型 |
| --- | --- |
| `HCOMM_DATA_TYPE_INT8` | int8 |
| `HCOMM_DATA_TYPE_INT16` | int16 |
| `HCOMM_DATA_TYPE_INT32` | int32 |
| `HCOMM_DATA_TYPE_FP16` | float16 |
| `HCOMM_DATA_TYPE_FP32` | float32 |
| `HCOMM_DATA_TYPE_BFP16` | bfp16 |
<!-- end id7 -->
<!-- npu="910b" id8 -->
针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，`dataType`参数支持的`HcommDataType`枚举值见表2。

表2 数据类型

| `HcommDataType`枚举值 | 数据类型 |
| --- | --- |
| `HCOMM_DATA_TYPE_INT8` | int8 |
| `HCOMM_DATA_TYPE_INT16` | int16 |
| `HCOMM_DATA_TYPE_INT32` | int32 |
| `HCOMM_DATA_TYPE_FP16` | float16 |
| `HCOMM_DATA_TYPE_FP32` | float32 |
| `HCOMM_DATA_TYPE_BFP16` | bfp16 |
<!-- end id8 -->

## 返回值

表3 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- `count * sizeof(dataType)`不得超过本端源内存和对端目的内存的可用长度。
- `dataType`支持的枚举值请参见[数据类型](#section_hcommwritereduceonthread_datatype)；`reduceOp`仅支持`HCOMM_REDUCE_SUM`、`HCOMM_REDUCE_MAX`、`HCOMM_REDUCE_MIN`。
<!-- npu="950" id9 -->
针对Ascend 950PR/Ascend 950DT：

- 仅支持AICPU_TS场景在Device侧调用。
- 仅支持通信协议UB_CTP、UB_RTP、UBOE，不支持RoCE。
<!-- end id9 -->
<!-- npu="A3,910b" id10 -->
针对Atlas A3 训练系列产品/Atlas A3 推理系列产品和Atlas A2 训练系列产品/Atlas A2 推理系列产品：

- 支持CPU_TS场景在Host侧调用，也支持AICPU_TS场景在Device侧调用。
- CPU_TS和AICPU_TS场景均仅支持通信协议HCCS、HCCS_ONLY，不支持RoCE。
<!-- end id10 -->

## 调用示例

```cpp
// 申请通信线程资源。
CommEngine engine = CommEngine::COMM_ENGINE_CPU_TS; // Atlas A3 训练系列产品/Atlas A3 推理系列产品使用。
// Ascend 950PR/Ascend 950DT场景下，将engine设置为CommEngine::COMM_ENGINE_AICPU_TS。
uint32_t threadNum = 1;
uint32_t notifyNumPerThread = 1;
ThreadHandle thread;
HcclComm comm;
HcclThreadAcquire(comm, engine, threadNum, notifyNumPerThread, &thread);

// 申请通信通道资源。
uint32_t channelNum = 1;
HcclChannelDesc channelDesc;
HcclChannelDescInit(&channelDesc, channelNum);
ChannelHandle channel;
HcclChannelAcquire(comm, engine, &channelDesc, channelNum, &channel);

// 获取本端通信内存信息。
void * localBuffer;
uint64_t localBufferSize;
HcclGetHcclBuffer(comm, &localBuffer, &localBufferSize);
// 获取对端通信内存信息。
void * remoteBuffer;
uint64_t remoteBufferSize;
HcclChannelGetHcclBuffer(comm, channel, &remoteBuffer, &remoteBufferSize);
uint64_t count = std::min(localBufferSize / sizeof(int32_t), remoteBufferSize / sizeof(int32_t));

// 将本端内存和对端内存数据进行reduce，输出到对端内存上。
HcommWriteReduceOnThread(thread, channel, remoteBuffer, localBuffer, count, HCOMM_DATA_TYPE_INT32, HCOMM_REDUCE_SUM);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
