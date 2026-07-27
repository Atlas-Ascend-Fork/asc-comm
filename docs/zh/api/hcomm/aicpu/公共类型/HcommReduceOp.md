# HcommReduceOp

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

定义Hcomm数据面归约接口使用的操作类型枚举。

## 定义原型

```cpp
typedef enum {
    HCOMM_REDUCE_SUM = 0,    /* sum */
    HCOMM_REDUCE_PROD = 1,   /* prod */
    HCOMM_REDUCE_MAX = 2,    /* max */
    HCOMM_REDUCE_MIN = 3,    /* min */
    HCOMM_REDUCE_RESERVED = 255  /* reserved */
} HcommReduceOp;
```


## 约束说明

- `HCOMM_REDUCE_RESERVED`和未定义的枚举值不可作为有效归约操作类型传入接口。
