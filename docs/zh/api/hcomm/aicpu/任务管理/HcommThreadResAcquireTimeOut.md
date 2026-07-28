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

该接口在AICPU侧调用，用于设置当前AICPU执行上下文中，通信线程向RTSQ（Remote Transport Sequence Queue）提交任务时等待队列可用空间的超时时间。AICPU执行后续数据面接口并向RTSQ写入任务时，如果RTSQ队列已满，则阻塞等待；队列出现可用空间后继续提交任务，达到设置的非0超时时间后仍无可用空间时，对应的数据面接口返回错误。

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

## 超时机制说明

1. **调用和生效范围**
   - 本接口仅支持在AICPU_TS模式下的Device侧调用。
   - 设置的超时时间保存在当前AICPU执行上下文中，对该上下文后续数据面任务获取RTSQ可用空间的操作生效。

2. **触发机制**
   - AICPU向RTSQ写入任务时，如果队列可用空间不足，则等待RTSQ中的已有任务被消费并释放空间。
   - `timeOut`为非0值时，等待达到该时间后仍无可用空间，对应的数据面接口返回错误。
   - `timeOut`为0时不进行超时判断，AICPU持续等待直至RTSQ有可用空间。

3. **默认值**
   - 未调用本接口时，默认超时时间为1856秒（约30分钟）。

## 应用场景

该接口适用于AICPU连续下发多个数据面任务、RTSQ可能暂时没有可用空间的场景。通过设置合理的等待时间，可以避免AICPU因RTSQ长时间无法释放空间而持续阻塞。

## 约束说明

- 本接口仅支持AICPU_TS模式下在Device侧调用。
- 建议在AICPU调用数据面接口前设置合适的超时时间。
- 超时值为0时会一直等待，可能导致死锁风险，请谨慎使用。
- `timeOut`仅支持0或正整数，不支持负数或小数。

## 调用示例

通信线程、通道等资源需由Host侧预先申请。本示例仅展示在AICPU Kernel中设置超时时间：

```cpp
// 在AICPU上执行的Kernel函数。

// 设置RTSQ队列满时的等待超时时间为10分钟。
float timeout = 600;
if (HcommThreadResAcquireTimeOut(timeout) != HCCL_SUCCESS) {
    return 1;
}

// 或者设置为永不超时
float timeoutNever = 0;
if (HcommThreadResAcquireTimeOut(timeoutNever) != HCCL_SUCCESS) {
    return 1;
}
// 后续数据面接口向RTSQ提交任务时，使用上述超时时间。

```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
