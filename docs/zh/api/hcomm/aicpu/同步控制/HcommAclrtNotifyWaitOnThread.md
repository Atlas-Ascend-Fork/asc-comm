# HcommAclrtNotifyWaitOnThread

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：不支持
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

用于使通信线程等待Host Stream或其他通信线程通过ACL Runtime Notify发送的同步信号，收到信号后继续执行后续任务。可与[HcommAclrtNotifyRecordOnThread](HcommAclrtNotifyRecordOnThread.md)配合实现通信线程与其他执行流之间的同步。

## 函数原型

```cpp
int32_t HcommAclrtNotifyWaitOnThread(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>[ThreadHandle](../公共类型/ThreadHandle.md)类型的定义请参见对应公共类型文档。 |
| notifyId | 输入 | 同步信号ID，取值为通过[aclrtGetNotifyId](https://gitcode.com/cann/runtime/blob/master/docs/zh/api_ref/08_notify_management.md#aclrtGetNotifyId)接口获取到的有效notifyId。 |
| timeOut | 输入 | 超时时间，单位：毫秒。<br>&nbsp;&nbsp;&bull; 0：表示永久等待；<br>&nbsp;&nbsp;&bull; 非0（正整数）：配置的等待超时时间。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- 无。

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
aclrtNotify notify;
uint64_t notifyId;
aclrtCreateNotify(&(notify), ACL_NOTIFY_DEFAULT);
aclrtGetNotifyId(notify, &(notifyId));
// 发送同步信号。
HcommAclrtNotifyRecordOnThread(threads[0], notifyId);
// 等待同步信号。
uint32_t timeout = 1;
HcommAclrtNotifyWaitOnThread(threads[1], notifyId, timeout);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
