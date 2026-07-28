# HcommChannelNotifyWaitOnThread

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

该接口用于Channel两端的同步，通过指定通信线程等待指定Channel上的Notify同步信号。对端可调用[HcommChannelNotifyRecordOnThread](HcommChannelNotifyRecordOnThread.md)发送该信号。该接口支持设置本次等待的超时时间。

## 函数原型

```cpp
int32_t HcommChannelNotifyWaitOnThread(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。取值为0或通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄；Host CPU侧调用时，该参数无作用，可传入0。<br>ThreadHandle类型的定义可参见[ThreadHandle](../公共类型/ThreadHandle.md)。 |
| channel | 输入 | 通信通道句柄，取值为通过[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口获取到的有效通信通道句柄。关于channel的约束参见约束说明。<br>ChannelHandle类型的定义可参见[ChannelHandle](../公共类型/ChannelHandle.md)。 |
| localNotifyIdx | 输入 | 本地Notify索引。<br>取值范围：[0, notifyNum)。<br>notifyNum为[HcommChannelCreate](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/basic_resource_mgmt/HcommChannelCreate.md)或[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口传入的channelDesc参数中的notifyNum。 |
| timeOut | 输入 | 超时时间，单位：秒。<br>&nbsp;&nbsp;&bull; 0：表示永久等待；<br>&nbsp;&nbsp;&bull; 非0（正整数）：配置的等待超时时间。|

<!-- npu="950" id7 -->
**补充说明**：

- thread参数：

  针对Ascend 950PR/Ascend 950DT，CPU引擎RoCE场景下，“thread”参数无作用，传入0即可。CPU_TS/AICPU_TS场景下，“thread”参数为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的threads。

- timeOut参数：

  针对Ascend 950PR/Ascend 950DT的CPU引擎RoCE场景，需配置大于0的超时时间。
<!-- end id7 -->

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- 该接口需要配合[HcommChannelNotifyRecordOnThread](HcommChannelNotifyRecordOnThread.md)使用。
<!-- npu="950" id6 -->
- 针对Ascend 950PR/Ascend 950DT，支持AICPU_TS场景在Device侧调用，也支持在Host CPU侧调用。
- Host CPU侧调用时，仅支持通信协议RoCE。
<!-- end id6 -->
- Host CPU侧调用时，`thread`参数无作用，可传入0。

## 调用示例

```cpp
// 申请通信线程资源。
CommEngine engine = CommEngine::COMM_ENGINE_CPU_TS;
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

// 针对Ascend 950PR/Ascend 950DT，需要在Device侧调用以下接口。

// 通知对端。
HcommChannelNotifyRecordOnThread(thread, channel, 0);

// 数据面操作。
// ...

// 等待对端通知本端。
uint32_t notifyTimeout = 1800;
HcommChannelNotifyWaitOnThread(thread, channel, 0, notifyTimeout);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
