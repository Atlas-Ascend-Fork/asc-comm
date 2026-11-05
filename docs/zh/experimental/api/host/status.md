# 状态类型

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

`ccu_status_t`是Host侧接口的公共返回状态类型，用于表示kernel提交、同步和状态文本转换接口的执行结果。

## 类型定义

```cpp
typedef enum ccu_status_t {
    CCU_STATUS_SUCCESS = 0,
    CCU_STATUS_INVALID_ARGUMENT = 1,
    CCU_STATUS_VERSION_MISMATCH = 2,
    CCU_STATUS_INVALID_OBJECT = 3,
    CCU_STATUS_KERNEL_NOT_FOUND = 4,
    CCU_STATUS_SCHEMA_MISMATCH = 5,
    CCU_STATUS_RESOURCE_EXHAUSTED = 6,
    CCU_STATUS_RUNTIME_ERROR = 7,
    CCU_STATUS_TIMEOUT_RECOVERED = 8,
    CCU_STATUS_RECOVERY_FAILED = 9,
    CCU_STATUS_BUSY = 10,
    CCU_STATUS_CONTEXT_POISONED = 11
} ccu_status_t;
```

## 状态说明

具体接口可能只返回其中一部分状态值，以对应接口文档为准。

| 状态值 | 说明 |
| --- | --- |
| `CCU_STATUS_SUCCESS` | 操作成功 |
| `CCU_STATUS_INVALID_ARGUMENT` | 参数非法 |
| `CCU_STATUS_VERSION_MISMATCH` | 版本不匹配 |
| `CCU_STATUS_INVALID_OBJECT` | 对象非法 |
| `CCU_STATUS_KERNEL_NOT_FOUND` | 找不到特例化kernel |
| `CCU_STATUS_SCHEMA_MISMATCH` | kernel参数或特例化kernel不匹配 |
| `CCU_STATUS_RESOURCE_EXHAUSTED` | 资源不足 |
| `CCU_STATUS_RUNTIME_ERROR` | 运行时错误 |
| `CCU_STATUS_TIMEOUT_RECOVERED` | 同步超时后已恢复 |
| `CCU_STATUS_RECOVERY_FAILED` | 恢复失败 |
| `CCU_STATUS_BUSY` | 当前任务未完成或资源被占用 |
| `CCU_STATUS_CONTEXT_POISONED` | 当前实例状态不可恢复 |
