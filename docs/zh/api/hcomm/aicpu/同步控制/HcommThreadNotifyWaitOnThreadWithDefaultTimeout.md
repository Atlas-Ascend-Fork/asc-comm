# HcommThreadNotifyWaitOnThreadWithDefaultTimeout

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

该接口用于通信线程之间的同步，通过指定通信线程等待该线程上指定Notify的同步信号。其他通信线程可调用[HcommThreadNotifyRecordOnThread](HcommThreadNotifyRecordOnThread.md)发送该信号。该接口使用[HcommSetNotifyWaitTimeOut](HcommSetNotifyWaitTimeOut.md)设置的默认超时时间，无需传入超时参数。

## 函数原型

```cpp
int32_t HcommThreadNotifyWaitOnThreadWithDefaultTimeout(ThreadHandle thread, uint32_t notifyIdx)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>ThreadHandle类型定义可参考[ThreadHandle](../公共类型/ThreadHandle.md)。 |
| notifyIdx | 输入 | 需等待的Notify通知索引。<br>取值范围为：[0, notifyNumPerThread)。<br>notifyNumPerThread为[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口传入的同名参数。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 超时机制说明

1. **默认超时时间**
   - 默认超时时间通过[HcommSetNotifyWaitTimeOut](./HcommSetNotifyWaitTimeOut.md)接口设置
   - 如果未设置，AICPU_TS场景下Device侧默认超时时间为1836秒，CPU_TS场景下Host侧默认超时时间为1886秒

2. **超时生效条件**
   - 已设置的超时时间将应用于本接口的等待操作
   - 设置为0表示永久等待，不超时
   - 设置大于0表示具体的超时时间（单位：秒）

3. **CPU_TS场景特殊处理**
   - CPU_TS场景下，如果未手动设置默认超时时间，系统会在1836秒基础上增加50秒的偏移量，即默认超时时间为1886秒

## 约束说明

- 该接口需要配合[HcommThreadNotifyRecordOnThread](./HcommThreadNotifyRecordOnThread.md)使用。

- `thread`必须为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效通信线程句柄。

- `notifyIdx`必须小于当前通信线程的Notify数量，即获取`thread`时传入的`notifyNumPerThread`。

- 默认超时时间由同侧调用上下文中的[HcommSetNotifyWaitTimeOut](./HcommSetNotifyWaitTimeOut.md)设置；CPU_TS场景需在Host侧设置，AICPU_TS场景需在Device侧设置。未设置时，CPU_TS场景的默认值为1886秒，AICPU_TS场景的默认值为1836秒。

<!-- npu="950" id6 -->
- 针对Ascend 950PR/Ascend 950DT，仅支持AICPU_TS场景在Device侧调用。
<!-- end id6 -->

<!-- npu="A3,910b" id7 -->
- 针对如下产品型号：

    <!-- npu="A3" id10 -->
    Atlas A3 训练系列产品/Atlas A3 推理系列产品；
    <!-- end id10 -->

    <!-- npu="910b" id11 -->
    Atlas A2 训练系列产品/Atlas A2 推理系列产品；
    <!-- end id11 -->

    支持CPU_TS场景在Host侧调用，也支持AICPU_TS场景在Device侧调用。
<!-- end id7 -->

## 调用示例

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_CPU_TS;
aclrtStream streams[2];
ThreadHandle threads[2];
// 申请2条流，每条流2个Notify。
aclrtCreateStream(&streams[0]);
aclrtCreateStream(&streams[1]);
HcclResult result = HcclThreadAcquireWithStream(comm, engine, streams[0], 2, &threads[0]);
result = HcclThreadAcquireWithStream(comm, engine, streams[1], 2, &threads[1]);

// 1. 设置默认超时时间（可选，未设置则使用默认值1886秒）。
uint32_t defaultTimeout = 600;  // 10分钟。
HcommSetNotifyWaitTimeOut(defaultTimeout);

uint32_t notifyIdx = 0;
// 发送同步信号。
HcommThreadNotifyRecordOnThread(threads[0], threads[1], notifyIdx);
// 使用默认超时时间等待同步信号（无需手动传入超时参数）。
HcommThreadNotifyWaitOnThreadWithDefaultTimeout(threads[1], notifyIdx);
```

<!-- npu="950" id8 -->
在Ascend 950PR/Ascend 950DT上，该函数需要编译到Device侧使用：

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_AICPU_TS;
ThreadHandle threads[2];
uint32_t notifyNumPerThread = 2;
HcclThreadAcquire(comm, engine, 1, notifyNumPerThread, &threads[0]);
HcclThreadAcquire(comm, engine, 1, notifyNumPerThread, &threads[1]);

// 其余资源申请。
// 拷贝参数并Launch Kernel。

// 1. 设置默认超时时间（可选，未设置则使用默认值1836秒）。
uint32_t defaultTimeout = 1800;  // 30分钟。
HcommSetNotifyWaitTimeOut(defaultTimeout);

// Device侧算法编排。
uint32_t notifyIdx = 0;
// 发送同步信号。
HcommThreadNotifyRecordOnThread(threads[0], threads[1], notifyIdx);
// 使用默认超时时间等待同步信号（无需手动传入超时参数）。
HcommThreadNotifyWaitOnThreadWithDefaultTimeout(threads[1], notifyIdx);
```

<!-- end id8 -->

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。

## 相关接口

- [HcommSetNotifyWaitTimeOut](./HcommSetNotifyWaitTimeOut.md)：设置默认超时时间
- [HcommThreadNotifyRecordOnThread](./HcommThreadNotifyRecordOnThread.md)：发送Thread同步信号
- [HcommThreadNotifyWaitOnThread](./HcommThreadNotifyWaitOnThread.md)：等待Thread同步信号（手动指定超时时间）
