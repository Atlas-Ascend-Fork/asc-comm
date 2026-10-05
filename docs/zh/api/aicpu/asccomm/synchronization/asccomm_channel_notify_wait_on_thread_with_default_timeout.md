# asccomm_channel_notify_wait_on_thread_with_default_timeout

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

生成本地Notify等待任务，等待指定Channel上的同步信号，使用[asccomm_set_notify_wait_timeout](asccomm_set_notify_wait_timeout.md)设置的默认超时时间。

## 函数原型

```cpp
int32_t asccomm_channel_notify_wait_on_thread_with_default_timeout(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| localNotifyIdx | 输入 | 本地Notify索引。当前实现要求`localNotifyIdx < localNotifyNum - 1`，并使用本地Notify数组中`localNotifyIdx + 1`对应的Notify；第0项为内部保留项。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 通知等待任务已生成；不表示同步信号已到达。 |
| `ASCCOMM_E_PTR` | Thread或Channel句柄为0，或Thread资源、RTSQ本地缓存指针为空。 |
| `ASCCOMM_E_PARA` | Thread/Channel资源ABI、本地Notify索引或Notify资源参数不合法。 |
| `ASCCOMM_E_TIMEOUT` | 触发RTSQ提交时，等待队列空间超时。 |
| `ASCCOMM_E_INTERNAL` | 任务生成、驱动查询或配置、RTSQ缓存操作等内部执行失败。 |

## 约束说明

- `localNotifyIdx`必须小于Channel资源中可用于等待的本地Notify数量。
- 默认超时时间通过[asccomm_set_notify_wait_timeout](asccomm_set_notify_wait_timeout.md)在当前AICPU执行线程中设置；未设置时当前实现为1836秒。
- 默认超时时间为0表示永久等待，大于0表示具体等待秒数。
- 通知等待任务提交时机与Thread资源的提交策略一致；普通模式可立即提交，批量模式或缓存满时按当前实现提交。

## 调用示例

```cpp
uint32_t localNotifyIdx = 0;
// thread和channel已由前置流程准备。
// 使用asccomm_set_notify_wait_timeout设置的默认超时时间等待。
int32_t ret = asccomm_channel_notify_wait_on_thread_with_default_timeout(thread, channel, localNotifyIdx);
```
