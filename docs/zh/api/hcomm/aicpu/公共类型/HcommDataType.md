# HcommDataType

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

定义Hcomm数据面接口使用的数据类型枚举。

## 定义原型

```cpp
typedef enum {
    HCOMM_DATA_TYPE_INT8 = 0,      /* int8 */
    HCOMM_DATA_TYPE_INT16 = 1,     /* int16 */
    HCOMM_DATA_TYPE_INT32 = 2,     /* int32 */
    HCOMM_DATA_TYPE_FP16 = 3,      /* float16 */
    HCOMM_DATA_TYPE_FP32 = 4,      /* float32 */
    HCOMM_DATA_TYPE_INT64 = 5,     /* int64 */
    HCOMM_DATA_TYPE_UINT64 = 6,    /* uint64 */
    HCOMM_DATA_TYPE_UINT8 = 7,     /* uint8 */
    HCOMM_DATA_TYPE_UINT16 = 8,    /* uint16 */
    HCOMM_DATA_TYPE_UINT32 = 9,    /* uint32 */
    HCOMM_DATA_TYPE_FP64 = 10,     /* fp64 */
    HCOMM_DATA_TYPE_BFP16 = 11,    /* bfp16 */
    HCOMM_DATA_TYPE_INT128 = 12,   /* int128 */
    HCOMM_DATA_TYPE_HIF8 = 14,     /* hif8 */
    HCOMM_DATA_TYPE_FP8E4M3 = 15,  /* fp8e4m3 */
    HCOMM_DATA_TYPE_FP8E5M2 = 16,  /* fp8e5m2 */
    HCOMM_DATA_TYPE_FP8E8M0 = 17,  /* fp8e8m0 */
    HCOMM_DATA_TYPE_RESERVED = 255 /* reserved */
} HcommDataType;
```


## 约束说明

- 无。
