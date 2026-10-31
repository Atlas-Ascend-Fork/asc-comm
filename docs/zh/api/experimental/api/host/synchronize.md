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

等待当前context在指定user stream上的CCU mission。等待必须有界，并按返回状态执行cleanup。

## 函数原型

```cpp
ccu_status_t ccu_synchronize(
    ccu_context_t *context, void *stream, uint64_t timeout_ms);

const char *ccu_status_string(ccu_status_t status);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `context` | 输入 | `ccu_context_t *` | 当前context |
| `stream` | 输入 | `void *` | 提交mission的user stream |
| `timeout_ms` | 输入 | `uint64_t` | 等待超时时间；`0`使用context默认值 |
| `status` | 输入 | `ccu_status_t` | 要转换为文本的状态值 |

## `ccu_synchronize`返回值

| 返回值 | 说明 | 处理 |
| --- | --- | --- |
| `CCU_STATUS_SUCCESS` | mission成功完成 | 读取输出并进入cleanup |
| `CCU_STATUS_TIMEOUT_RECOVERED` | 当前任务已终止 | 不要追加任务，进入cleanup |
| `CCU_STATUS_RECOVERY_FAILED` | 恢复失败 | 停止使用context，进入最终cleanup |
| `CCU_STATUS_CONTEXT_POISONED` | context已污染 | 不得复用context，释放所属资源 |
| `CCU_STATUS_BUSY` | mission未能完成并释放 | 重新synchronize或进入cleanup |
| `CCU_STATUS_INVALID_ARGUMENT` | 参数非法 | 检查context/stream |
| `CCU_STATUS_RUNTIME_ERROR` | 运行时错误 | 停止使用context并cleanup |

## `ccu_status_string`返回值

返回稳定的错误状态文本。未知状态值返回`"unknown status"`。

## 约束说明

- 所有等待必须有界，不能使用无界stream synchronize替代。
- 同步失败后不要读取未完成的输出。
- 进入终止状态的context不能继续提交mission。
- cleanup应覆盖成功和失败路径。

## 调用示例

```cpp
// 1. 在 user stream 上等待当前 mission，最多 30 秒。
ccu_status_t status = ccu_synchronize(context, stream, 30000);

// 2. 成功时读取输出。
if (status == CCU_STATUS_SUCCESS) {
  verify_output();
} else {
  // 3. 失败时打印稳定诊断文本，并进入 cleanup。
  fprintf(stderr, "synchronize failed: %s\n", ccu_status_string(status));
}
```
