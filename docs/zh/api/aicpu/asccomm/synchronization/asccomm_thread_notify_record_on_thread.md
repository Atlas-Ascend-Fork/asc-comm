# asccomm_thread_notify_record_on_thread

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

向目标Thread的Notify发送同步信号，主要用于多Thread之间的同步等待场景。

## 函数原型

```cpp
int32_t asccomm_thread_notify_record_on_thread(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| dstThread | 输入 | 目标Thread句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| dstNotifyIdx | 输入 | 目标Thread的Notify索引。取值范围为`[0, dstThread资源中的Notify数量)`。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 通知任务已生成；不表示目标Notify已收到同步信号。 |
| `ASCCOMM_E_PTR` | `thread`或`dstThread`为0，或Thread资源、RTSQ本地缓存指针为空。 |
| `ASCCOMM_E_PARA` | Thread资源ABI、RTSQ资源、目标Notify索引或Notify资源参数不合法。 |
| `ASCCOMM_E_TIMEOUT` | 触发RTSQ提交时，等待队列空间超时。 |
| `ASCCOMM_E_INTERNAL` | 任务生成、驱动查询或配置、RTSQ缓存操作等内部执行失败。 |

## 约束说明

- `dstNotifyIdx`必须小于目标Thread资源的Notify数量，且对应Notify资源须为当前实现支持的类型。
- 该接口需与[asccomm_thread_notify_wait_on_thread](asccomm_thread_notify_wait_on_thread.md)配合使用。
- 通知任务提交时机与Thread资源的提交策略一致；普通模式可立即提交，批量模式或缓存满时按当前实现提交。

## 调用示例

```cpp
// thread和dstThread均为控制面下发的有效Thread资源句柄。
uint32_t notifyIdx = 0;
int32_t ret = asccomm_thread_notify_record_on_thread(thread, dstThread, notifyIdx);
```
