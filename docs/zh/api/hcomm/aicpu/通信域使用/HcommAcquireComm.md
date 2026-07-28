# HcommAcquireComm

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

根据传入的commId获取对应通信域，并对该通信域加锁，防止该通信域被重复获取。

## 函数原型

```cpp
int32_t HcommAcquireComm(const char* commId)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| commId | 输入 | 通信域ID。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- 仅支持AI CPU模式下，在Device侧调用该接口。

- 本接口和[HcommReleaseComm](HcommReleaseComm.md)分别对应加锁和解锁动作，必须成对调用。接口内部会拦截重复加锁场景，避免同一个通信域被多个线程同时占用。

## 调用示例

该函数需编译到Device侧使用：

```cpp
// 在AI CPU上执行的Kernel函数。
extern "C" unsigned int HcclAicpuKernel(const char* commId)
{
    // 对通信域加锁，防止该通信域被并发使用。
    if (HcommAcquireComm(commId) != HCCL_SUCCESS) {
        return 1;
    }

    // 执行任务编排。
    // ...

    // 释放通信域。
    if (HcommReleaseComm(commId) != HCCL_SUCCESS) {
        return 1;
    }
    return 0;
}
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
