# HcommSetNotifyWaitTimeOut

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

设置通知等待的默认超时时间。该超时时间将应用于后续调用的[HcommChannelNotifyWaitOnThreadWithDefaultTimeout](./HcommChannelNotifyWaitOnThreadWithDefaultTimeout.md)和[HcommThreadNotifyWaitOnThreadWithDefaultTimeout](./HcommThreadNotifyWaitOnThreadWithDefaultTimeout.md)接口。

## 函数原型

```cpp
int32_t HcommSetNotifyWaitTimeOut(float timeOut)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | -------------- |
| timeOut | 输入 | 通知等待超时时间。单位：秒。取值范围为[0, `UINT32_MAX`]<br>取值说明：<br>&nbsp;&nbsp;&bull; 0：表示永久等待；<br>&nbsp;&nbsp;&bull; 非0（正整数）：配置的等待超时时间。|

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- 本接口设置的超时时间仅对[HcommChannelNotifyWaitOnThreadWithDefaultTimeout](./HcommChannelNotifyWaitOnThreadWithDefaultTimeout.md)和[HcommThreadNotifyWaitOnThreadWithDefaultTimeout](./HcommThreadNotifyWaitOnThreadWithDefaultTimeout.md)接口生效。
- 手动传入超时时间的[HcommChannelNotifyWaitOnThread](./HcommChannelNotifyWaitOnThread.md)和[HcommThreadNotifyWaitOnThread](./HcommThreadNotifyWaitOnThread.md)接口不受本接口影响。
- `timeOut`传入小数时会转换为`uint32_t`类型，小数部分会被截去，建议传入整数。取值范围为[0, `UINT32_MAX`]，单位为秒。0表示永久等待，非0值表示具体的等待超时时间。

- 建议在数据面操作开始前设置默认超时时间。
<!-- npu="950" id6 -->
针对Ascend 950PR/Ascend 950DT：

- 支持CPU_TS场景在Host侧调用，也支持AICPU_TS场景在Device侧调用。
- CPU_TS场景下，未调用本接口时的默认超时时间为1886秒；AICPU_TS场景下，未调用本接口时的默认超时时间为1836秒。
<!-- end id6 -->
<!-- npu="A3,910b" id7 -->
针对Atlas A3 训练系列产品/Atlas A3 推理系列产品和Atlas A2 训练系列产品/Atlas A2 推理系列产品：

- 支持CPU_TS场景在Host侧调用，也支持AICPU_TS场景在Device侧调用。
- CPU_TS场景下，未调用本接口时的默认超时时间为1886秒；AICPU_TS场景下，未调用本接口时的默认超时时间为1836秒。
<!-- end id7 -->

## 调用示例

```cpp
// 1. 设置默认超时时间（例如设置为10分钟）。
float defaultTimeOut = 600;  // 600秒 = 10分钟。
HcommSetNotifyWaitTimeOut(defaultTimeOut);

// 2. 申请通信线程资源。
CommEngine engine = COMM_ENGINE_AICPU_TS;
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

// 6. 使用默认超时时间等待通知（无需手动传入超时参数）。
HcommChannelNotifyWaitOnThreadWithDefaultTimeout(thread, channel, 0);

// 或者使用线程通知等待。
HcommThreadNotifyWaitOnThreadWithDefaultTimeout(thread, 0);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。

## 相关接口

- [HcommChannelNotifyWaitOnThreadWithDefaultTimeout](./HcommChannelNotifyWaitOnThreadWithDefaultTimeout.md)：使用默认超时时间等待Channel通知。
- [HcommThreadNotifyWaitOnThreadWithDefaultTimeout](./HcommThreadNotifyWaitOnThreadWithDefaultTimeout.md)：使用默认超时时间等待Thread通知。
- [HcommChannelNotifyWaitOnThread](./HcommChannelNotifyWaitOnThread.md)：手动指定超时时间等待Channel通知。
- [HcommThreadNotifyWaitOnThread](./HcommThreadNotifyWaitOnThread.md)：手动指定超时时间等待Thread通知。
