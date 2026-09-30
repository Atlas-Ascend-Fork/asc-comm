# asccomm_thread_notify_wait_on_thread_with_default_timeout

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

等待其他Thread发送的同步信号，主要用于多Thread之间的同步等待场景。**使用通过[asccomm_set_notify_wait_timeout](./asccomm_set_notify_wait_timeout.md)设置的默认超时时间**，无需手动传入超时参数。

该接口需与[asccomm_thread_notify_record_on_thread](./asccomm_thread_notify_record_on_thread.md)配合使用；如需手动指定超时时间，可调用[asccomm_thread_notify_wait_on_thread](./asccomm_thread_notify_wait_on_thread.md)。

## 函数原型

```cpp
int32_t asccomm_thread_notify_wait_on_thread_with_default_timeout(ThreadHandle thread, uint32_t notifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| notifyIdx | 输入 | 需等待的Notify通知索引。<br>取值范围为：［0，目标Thread资源中的Notify数量）。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- `notifyIdx`必须小于本端Thread资源的Notify数量。
- 默认超时时间通过[asccomm_set_notify_wait_timeout](asccomm_set_notify_wait_timeout.md)设置；未设置时为1836秒。
- 默认超时时间为0表示永久等待，大于0表示具体等待秒数。

## 调用示例

```cpp
uint32_t notifyIdx = 0;
// 使用asccomm_set_notify_wait_timeout设置的默认超时时间等待。
int32_t ret = asccomm_thread_notify_wait_on_thread_with_default_timeout(thread, notifyIdx);
```
