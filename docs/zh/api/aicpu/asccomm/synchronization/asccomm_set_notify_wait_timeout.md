# asccomm_set_notify_wait_timeout

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
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->
<!-- npu="310p" id6 -->
- Atlas 推理系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径：`"asccomm/asccomm_primitives.h"`。

设置通知等待的默认超时时间。该超时时间将应用于后续调用的[asccomm_channel_notify_wait_on_thread_with_default_timeout](./asccomm_channel_notify_wait_on_thread_with_default_timeout.md)和[asccomm_thread_notify_wait_on_thread_with_default_timeout](./asccomm_thread_notify_wait_on_thread_with_default_timeout.md)接口。

手动指定超时时间的[asccomm_channel_notify_wait_on_thread](./asccomm_channel_notify_wait_on_thread.md)和[asccomm_thread_notify_wait_on_thread](./asccomm_thread_notify_wait_on_thread.md)接口不受本接口影响。

## 函数原型

```cpp
int32_t asccomm_set_notify_wait_timeout(float timeOut)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | -------------- |
| timeOut | 输入 | 通知等待超时时间。<br>单位：秒。<br>取值说明：<br>  - 0：表示永久等待，不超时。<br>  - >0：具体的超时时间，传入小数时向零截断为整数秒。<br>默认值：1836秒（约30分钟）。<br>取值不能为负数、NaN或大于`UINT32_MAX`。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- 本接口设置的超时时间仅对`*WithDefaultTimeout`系列接口生效。
- 手动传入超时时间的[asccomm_channel_notify_wait_on_thread](asccomm_channel_notify_wait_on_thread.md)和[asccomm_thread_notify_wait_on_thread](asccomm_thread_notify_wait_on_thread.md)不受本接口影响。
- 未设置时默认超时时间为1836秒。
- `timeOut`为非负浮点数时允许传入，但会向零截断为整数秒；负数、NaN和大于`UINT32_MAX`的取值返回`ASCCOMM_E_PARA`。

## 调用示例

```cpp
float timeout = 1800.0f;  // 1800秒
int32_t ret = asccomm_set_notify_wait_timeout(timeout);
```
