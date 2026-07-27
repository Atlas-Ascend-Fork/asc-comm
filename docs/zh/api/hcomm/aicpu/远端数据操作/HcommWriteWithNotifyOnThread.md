# HcommWriteWithNotifyOnThread

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310p" id4 -->
- Atlas 推理系列产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->

## 功能说明

头文件：[`#include "hcomm_primitives.h"`](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_primitives.h)。

向channel上的指定内存写数据，将源地址`src`中长度为`len`的数据写入目的地址`dst`，并向目的地址`dst`所在节点发送同步信号。接口调用方为源地址`src`所在节点，该接口为异步接口。

## 函数原型

```cpp
int32_t HcommWriteWithNotifyOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len, uint32_t remoteNotifyIdx)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>ThreadHandle类型的定义可参见[ThreadHandle](../公共类型/ThreadHandle.md)。 |
| channel | 输入 | 通信通道句柄，取值为通过[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口获取到的有效通信通道句柄。<br>ChannelHandle类型的定义可参见[ChannelHandle](../公共类型/ChannelHandle.md)。 |
| dst | 输出 | 目的内存地址，使用[HcclGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclGetHcclBuffer.md)、[HcclChannelGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetHcclBuffer.md)获取到的内存。 |
| src | 输入 | 源内存地址，使用[HcclGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclGetHcclBuffer.md)、[HcclChannelGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetHcclBuffer.md)获取到的内存。 |
| len | 输入 | 待写入的数据长度。单位：字节。有效范围由源内存和目的内存的可用长度决定，不得超过两者中较小的可用长度。 |
| remoteNotifyIdx | 输入 | 通信通道另一端的Notify索引。<br>取值范围：[0, notifyNum)。<br>notifyNum为[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口传入的channelDescs参数中的notifyNum。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- 该接口需要配合[HcommChannelNotifyWaitOnThread](../同步控制/HcommChannelNotifyWaitOnThread.md)使用。

<!-- npu="950" id6 -->
- 在Ascend 950PR/Ascend 950DT上，仅支持AICPU_TS模式下、在Device侧调用该接口。
<!-- end id6 -->

## 调用示例

```cpp
// 申请通信线程资源。
CommEngine engine = CommEngine::COMM_ENGINE_AICPU_TS;
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
uint64_t len = std::min(localBufferSize, remoteBufferSize);

// 针对Ascend 950PR/Ascend 950DT，需要在Device侧调用以下接口。

// 将本端内存的内容写到对端内存上并通知对端。
uint32_t rmtNotifyIdx = 0;
HcommWriteWithNotifyOnThread(thread, channel, remoteBuffer, localBuffer, len, rmtNotifyIdx);

// 数据面操作。
// ...

// 等待对端通知本端。
uint32_t lclNotifyIdx = 0;
uint32_t notifyTimeout = 0;
HcommChannelNotifyWaitOnThread(thread, channel, lclNotifyIdx, notifyTimeout);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
