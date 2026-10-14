# asccomm_set_notify_wait_timeout

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

头文件路径：`"aicpu/hcomm/asccomm_primitives.h"`。

设置当前AICPU执行线程通知等待的默认超时时间。该超时时间将应用于当前执行线程后续调用的[asccomm_channel_notify_wait_on_thread_with_default_timeout](./asccomm_channel_notify_wait_on_thread_with_default_timeout.md)和[asccomm_thread_notify_wait_on_thread_with_default_timeout](./asccomm_thread_notify_wait_on_thread_with_default_timeout.md)接口。

手动指定超时时间的[asccomm_channel_notify_wait_on_thread](./asccomm_channel_notify_wait_on_thread.md)和[asccomm_thread_notify_wait_on_thread](./asccomm_thread_notify_wait_on_thread.md)接口不受本接口影响。

## 函数原型

```cpp
int32_t asccomm_set_notify_wait_timeout(float timeOut)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | -------------- |
| timeOut | 输入 | 通知等待超时时间，单位：秒。<br>0表示永久等待；大于0表示指定超时时间，传入小数时向零截断为整数秒。<br>取值不能为负数、NaN或大于`UINT32_MAX`。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 默认超时时间设置成功。 |
| `ASCCOMM_E_PARA` | `timeOut`为负数、NaN或大于`UINT32_MAX`。 |

## 约束说明

- 本接口设置的超时时间仅对当前AICPU执行线程的`*WithDefaultTimeout`系列接口生效。
- 手动传入超时时间的[asccomm_channel_notify_wait_on_thread](asccomm_channel_notify_wait_on_thread.md)和[asccomm_thread_notify_wait_on_thread](asccomm_thread_notify_wait_on_thread.md)不受本接口影响。
- 未设置时，当前实现默认超时时间为1836秒。
- `timeOut`为非负浮点数时允许传入，但会向零截断为整数秒；负数、NaN和大于`UINT32_MAX`的取值返回`ASCCOMM_E_PARA`。

## 调用示例

```cpp
// timeout为示例选择的等待时长，实际取值由调用方根据同步策略确定。
float timeout = 1800.0f;  // 1800秒
int32_t ret = asccomm_set_notify_wait_timeout(timeout);
```
