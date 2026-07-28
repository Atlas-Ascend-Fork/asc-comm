# HcommTransferType

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

定义批量传输操作中的传输类型枚举，用于[HcommBatchTransferDesc](HcommBatchTransferDesc.md)结构体中的`transType`字段，指定单个传输描述符执行的操作类型。

## 定义原型<a name="section_hcommtransfertype_definition"></a>

```cpp
typedef enum {
    HCOMM_TRANSFER_TYPE_INVALID = -1,                       /* 无效传输类型 */
    HCOMM_TRANSFER_TYPE_WRITE = 0,                          /* 单边写，对应transferInfo.write */
    HCOMM_TRANSFER_TYPE_WRITE_REDUCE = 1,                   /* 单边写规约，对应transferInfo.reduce */
    HCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY = 2,              /* 带通知的单边写，对应transferInfo.writeWithNotify */
    HCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY = 3,       /* 带通知的单边写规约，对应transferInfo.writeReduceWithNotify */
    HCOMM_TRANSFER_TYPE_READ = 4,                           /* 单边读，对应transferInfo.read */
    HCOMM_TRANSFER_TYPE_READ_REDUCE = 5,                    /* 单边读规约，对应transferInfo.reduce */
    HCOMM_TRANSFER_TYPE_NOTIFY_RECORD = 6                   /* 记录通知事件，对应transferInfo.notifyRecord */
} HcommTransferType;
```

## 约束说明

- `transType`必须与实际填写的`transferInfo`联合体成员一致，映射关系见[定义原型](#section_hcommtransfertype_definition)。

<!-- npu="A3,910b" id6 -->
- 针对如下产品型号：

    <!-- npu="A3" id7 -->
    Atlas A3 训练系列产品/Atlas A3 推理系列产品；
    <!-- end id7 -->

    <!-- npu="910b" id8 -->
    Atlas A2 训练系列产品/Atlas A2 推理系列产品；
    <!-- end id8 -->

    使用批量传输接口时，仅支持`HCOMM_TRANSFER_TYPE_WRITE`和`HCOMM_TRANSFER_TYPE_READ`，其他类型返回不支持。
<!-- end id6 -->
