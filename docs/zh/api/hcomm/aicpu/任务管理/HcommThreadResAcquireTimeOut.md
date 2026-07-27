# HcommThreadResAcquireTimeOut

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

该接口用于设置通信线程向RTSQ（Remote Transport Sequence Queue）提交任务时，RTSQ队列满等待可用空间的超时时间。RTSQ队列已满时，通信线程将阻塞等待；队列出现可用空间后继续提交任务，达到设置的非0超时时间后仍无可用空间时，对应的数据面接口返回错误。

## 函数原型

```cpp
int32_t HcommThreadResAcquireTimeOut(float timeOut)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| timeOut | 输入 | RTSQ队列满时等待可用空间的超时时间。<br>单位：秒。<br>取值说明：<br>&nbsp;&nbsp;&bull; 0：表示永不超时，等待直到RTSQ有可用空间；<br>&nbsp;&nbsp;&bull; 非0（正整数）：表示具体的等待超时时间。<br>&nbsp;&nbsp;&bull; 默认值：1856秒。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 超时机制应用场景

当数据面操作向RTSQ提交任务时，如果队列已满，通信线程将等待队列释放空间，从而实现队列背压和流量控制。该机制适用于任务提交较快、RTSQ可能暂时无可用空间的场景；设置非0超时时间可以限制最长等待时间，避免通信线程无限等待。

## 约束说明

- 该接口设置的超时时间会影响所有线程的资源获取操作。
- 建议在初始化阶段或资源申请前设置合适的超时值。
- 超时值为0时会一直等待，可能导致死锁风险，请谨慎使用。
- `timeOut`仅支持0或正整数，不支持负数或小数。

## 调用示例

```cpp
// 1. 设置RTSQ队列满等待超时时间（例如设置为10分钟）。
float timeout = 600;  // 600秒 = 10分钟。
HcommThreadResAcquireTimeOut(timeout);

// 或者设置为永不超时（0表示永不超时）。
float timeoutNever = 0;
HcommThreadResAcquireTimeOut(timeoutNever);

// 2. 申请通信线程资源。
CommEngine engine = COMM_ENGINE_AICPU_TS;
uint32_t threadNum = 1;
uint32_t notifyNumPerThread = 1;
ThreadHandle thread;
HcclComm comm;
HcclThreadAcquire(comm, engine, threadNum, notifyNumPerThread, &thread);

// 3. 数据面操作（RTSQ满时会等待可用空间或超时）。
// ... 执行数据发送操作 ...。
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。

## 相关接口

[HcommSetNotifyWaitTimeOut](../同步控制/HcommSetNotifyWaitTimeOut.md)：设置通知等待超时时间。
