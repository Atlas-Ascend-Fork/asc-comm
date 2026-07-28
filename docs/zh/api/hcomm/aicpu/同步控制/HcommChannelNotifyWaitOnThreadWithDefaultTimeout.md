# HcommChannelNotifyWaitOnThreadWithDefaultTimeout

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

该接口用于Channel两端的同步，通过指定通信线程等待指定Channel上的Notify同步信号。对端可调用[HcommChannelNotifyRecordOnThread](HcommChannelNotifyRecordOnThread.md)发送该信号。该接口使用[HcommSetNotifyWaitTimeOut](HcommSetNotifyWaitTimeOut.md)设置的默认超时时间，无需传入超时参数。

## 函数原型

```cpp
int32_t HcommChannelNotifyWaitOnThreadWithDefaultTimeout(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>[ThreadHandle](../公共类型/ThreadHandle.md)类型定义可参考对应公共类型文档。 |
| channel | 输入 | 通信通道句柄，取值为通过[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口获取到的有效通信通道句柄。<br>[ChannelHandle](../公共类型/ChannelHandle.md)类型定义可参考对应公共类型文档。 |
| localNotifyIdx | 输入 | 本地Notify索引。<br>取值范围：[0, notifyNum)。<br>notifyNum为[HcommChannelCreate](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/basic_resource_mgmt/HcommChannelCreate.md)或[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口传入的channelDesc参数中的notifyNum。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |


## 约束说明

- 该接口需要配合[HcommChannelNotifyRecordOnThread](HcommChannelNotifyRecordOnThread.md)使用。
- `localNotifyIdx`必须小于本端通信通道的Notify数量，且通信通道创建时的`notifyNum`需大于0。
- 本接口的超时时间由[HcommSetNotifyWaitTimeOut](HcommSetNotifyWaitTimeOut.md)设置；未设置时，Device侧默认超时时间为1836秒。
<!-- npu="950" id6 -->
针对Ascend 950PR/Ascend 950DT：

- 仅支持AICPU_TS场景在Device侧调用。
- 支持通信协议UB_CTP、UB_RTP、UBOE、RoCE，`thread`必须为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效通信线程句柄。
<!-- end id6 -->
<!-- npu="A3,910b" id7 -->
针对Atlas A3 训练系列产品/Atlas A3 推理系列产品和Atlas A2 训练系列产品/Atlas A2 推理系列产品：

- 仅支持AICPU_TS场景在Device侧调用。
- 支持通信协议HCCS、HCCS_ONLY、RoCE，`thread`必须为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效通信线程句柄。
<!-- end id7 -->

## 调用示例

```cpp
// 1. 设置默认超时时间（可选，未设置则使用默认值1836秒）。
uint32_t defaultTimeout = 1800;  // 30分钟。
HcommSetNotifyWaitTimeOut(defaultTimeout);

// 2. 申请通信线程资源。
CommEngine engine = COMM_ENGINE_AICPU_TS;  // Ascend 950PR/Ascend 950DT时配置。
uint32_t threadNum = 1;
uint32_t notifyNumPerThread = 1;
ThreadHandle thread;
HcclComm comm;
HcclThreadAcquire(comm, engine, threadNum, notifyNumPerThread, &thread);

// 3. 申请通信通道资源。
uint32_t channelNum = 1;
HcclChannelDesc channelDesc;
HcclChannelDescInit(&channelDesc, channelNum);
ChannelHandle channel;
HcclChannelAcquire(comm, engine, &channelDesc, channelNum, &channel);

// 4. 在Device侧记录通知。
HcommChannelNotifyRecordOnThread(thread, channel, 0);

// 5. 数据面操作。
// ...

// 6. 等待对端通知（使用默认超时时间，无需手动传入）。
HcommChannelNotifyWaitOnThreadWithDefaultTimeout(thread, channel, 0);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
