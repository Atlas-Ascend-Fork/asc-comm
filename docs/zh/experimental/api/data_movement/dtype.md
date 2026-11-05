# 数据类型

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件为：`#include <ccu_intrinsics.h>`

`__ccu_dtype_t`表示数据搬运和归约接口的元素数据类型。

## 类型定义

```cpp
typedef enum __ccu_dtype_t {
  CCU_DTYPE_INT8 = 0,
  CCU_DTYPE_INT16 = 1,
  CCU_DTYPE_INT32 = 2,
  CCU_DTYPE_FP16 = 3,
  CCU_DTYPE_FP32 = 4,
  CCU_DTYPE_BF16 = 5,
  CCU_DTYPE_UINT8 = 6,
  CCU_DTYPE_UINT16 = 7,
  CCU_DTYPE_UINT32 = 8
} __ccu_dtype_t;
```

## 取值说明

| 枚举值 | 数据类型 |
| --- | --- |
| `CCU_DTYPE_INT8` | 8位有符号整数 |
| `CCU_DTYPE_INT16` | 16位有符号整数 |
| `CCU_DTYPE_INT32` | 32位有符号整数 |
| `CCU_DTYPE_FP16` | 16位浮点数 |
| `CCU_DTYPE_FP32` | 32位浮点数 |
| `CCU_DTYPE_BF16` | 16位BF16浮点数 |
| `CCU_DTYPE_UINT8` | 8位无符号整数 |
| `CCU_DTYPE_UINT16` | 16位无符号整数 |
| `CCU_DTYPE_UINT32` | 32位无符号整数 |

## 约束说明

- `dtype`必须是上表中的枚举值。
- `dtype`必须是编译期常量。
- 具体接口支持的取值以对应接口文档为准。

