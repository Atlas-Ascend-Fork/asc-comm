# 同步、超时与清理

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

`ccu_synchronize`等待当前instance在指定stream上的CCU kernel任务。等待必须设置有限超时时间，并按返回状态释放资源。

`ccu_status_string`配合`ccu_status_t`返回状态描述字符串，用于日志打印或错误提示。

## 函数原型

```cpp
ccu_status_t ccu_synchronize(
    ccu_instance_t instance, void *stream, uint64_t timeout_ms);

const char *ccu_status_string(ccu_status_t status);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `instance` | 输入 | `ccu_instance_t` | HCOMM提供的instance句柄 |
| `stream` | 输入 | `void *` | 提交kernel任务的stream |
| `timeout_ms` | 输入 | `uint64_t` | 等待超时时间，单位为毫秒 |
| `status` | 输入 | `ccu_status_t` | 要转换为文本的状态值 |

## `ccu_synchronize`返回值

| 返回值 | 说明 | 处理 |
| --- | --- | --- |
| `CCU_STATUS_SUCCESS` | 任务成功完成 | 读取输出并释放资源 |
| `CCU_STATUS_TIMEOUT_RECOVERED` | 当前任务已终止 | 不要追加任务，释放资源 |
| `CCU_STATUS_RECOVERY_FAILED` | 恢复失败 | 释放资源并退出失败路径 |
| `CCU_STATUS_CONTEXT_POISONED` | 当前实例状态不可恢复 | 释放所属资源 |
| `CCU_STATUS_BUSY` | 任务未能完成并释放 | 重新synchronize或释放资源 |
| `CCU_STATUS_INVALID_ARGUMENT` | 参数非法 | 检查instance/stream |
| `CCU_STATUS_RUNTIME_ERROR` | 运行时错误 | 释放资源并退出失败路径 |

完整状态定义见[状态类型](status.md)。

## `ccu_status_string`返回值

返回状态描述字符串。未知状态值返回`"unknown status"`。

## 约束说明

- 所有等待必须设置有限超时时间，不能无限等待。
- 同步失败后不要读取未完成的输出。
- 进入终止状态的instance不能继续提交任务。
- 成功和失败路径都必须释放已创建的资源。

## 调用示例

```cpp
// 1. 在stream上等待当前kernel任务，最多30秒。
ccu_status_t status = ccu_synchronize(instance, stream, 30000);

// 2. 成功时读取输出。
if (status == CCU_STATUS_SUCCESS) {
  verify_output();
} else {
  // 3. 失败时打印稳定诊断文本，并释放资源。
  fprintf(stderr, "synchronize failed: %s\n", ccu_status_string(status));
}
```
