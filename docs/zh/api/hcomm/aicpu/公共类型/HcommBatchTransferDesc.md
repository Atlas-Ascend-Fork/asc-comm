# HcommBatchTransferDesc

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

定义批量传输描述符结构体，用于描述单个传输任务。通过`transType`指定传输类型，由`transferInfo`联合体提供对应类型的详细参数。

## 定义原型

```cpp
typedef struct {
    HcommTransferType transType;    /* 传输类型，参见HcommTransferType */
    uint8_t reserved[4];            /* 保留字段 */
    union {
        uint8_t raws[56];           /* 原始字节访问，用于零拷贝序列化等场景 */
        struct {
            uint64_t len;           /* 数据长度（字节） */
            void *dst;              /* 远端目的地址 */
            void *src;              /* 本地源地址 */
        } write;
        struct {
            uint64_t len;           /* 数据长度（字节） */
            void *dst;              /* 本地目的地址 */
            void *src;              /* 远端源地址 */
        } read;
        struct {
            uint64_t count;         /* 元素个数 */
            void *dst;              /* 远端目的地址 */
            void *src;              /* 本地源地址 */
            HcommReduceOp reduceOp; /* 规约操作类型 */
            HcommDataType dataType; /* 数据类型 */
        } reduce;
        struct {
            uint32_t notifyIdx;     /* 通知索引 */
        } notifyRecord;
        struct {
            uint64_t len;           /* 数据长度（字节） */
            void *dst;              /* 远端目的地址 */
            void *src;              /* 本地源地址 */
            uint32_t notifyIdx;     /* 写完成后通知的远端通知索引 */
        } writeWithNotify;
        struct {
            uint64_t count;         /* 元素个数 */
            void *dst;              /* 远端目的地址 */
            void *src;              /* 本地源地址 */
            HcommReduceOp reduceOp; /* 规约操作类型 */
            HcommDataType dataType; /* 数据类型 */
            uint32_t notifyIdx;     /* 写规约完成后通知的远端通知索引 */
        } writeReduceWithNotify;
    } transferInfo;
} HcommBatchTransferDesc;
```


## 约束说明

- `transType`必须使用[HcommTransferType](HcommTransferType.md)中有效的传输类型，并按该类型填写对应的`transferInfo`联合体成员；
- Atlas A3 训练系列产品/Atlas A3 推理系列产品和Atlas A2 训练系列产品/Atlas A2 推理系列产品仅支持RoCE协议，且`transType`仅支持`HCOMM_TRANSFER_TYPE_WRITE`和`HCOMM_TRANSFER_TYPE_READ`。
