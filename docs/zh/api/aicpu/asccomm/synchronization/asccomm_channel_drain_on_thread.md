# asccomm_channel_drain_on_thread

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

等待指定Channel上已提交的通信操作完成。

本接口用于等待调用前已经提交的任务完成，不为调用后提交的任务增加保序约束。若需要保证屏障前后的通道读写操作顺序，请调用[asccomm_channel_fence_on_thread](asccomm_channel_fence_on_thread.md)。

## 函数原型

```cpp
int32_t asccomm_channel_drain_on_thread(ThreadHandle thread, ChannelHandle channel)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，包含`"aicpu/hcomm/asccomm_primitives.h"`后即可使用。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，包含`"aicpu/hcomm/asccomm_primitives.h"`后即可使用。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 等待处理成功。[RoCE协议](../../../../programming_model/communication_pattern_selection.md#通信协议分类与选择)或[UB类协议](../../../../programming_model/communication_pattern_selection.md#通信协议分类与选择)内部drain资源缺失时直接返回成功。 |
| `ASCCOMM_E_PTR` | Thread或Channel句柄为0，或Thread资源、本地任务缓存指针为空。 |
| `ASCCOMM_E_PARA` | Thread/Channel资源、通信任务提交队列或UB drain资源参数不合法。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel协议既不是当前支持的UB类协议，也不是RoCE协议。 |
| `ASCCOMM_E_TIMEOUT` | 触发任务提交时，等待通信任务提交队列空间超时。 |
| `ASCCOMM_E_INTERNAL` | 任务生成、驱动查询或配置、本地任务缓存操作等内部执行失败。 |

## 约束说明

- 当前实现支持UB类协议和RoCE协议；其他协议返回`ASCCOMM_E_NOT_SUPPORT`。
- RoCE协议调用本接口时直接返回成功，不执行额外等待。
- UB类协议通过内部完成通知等待；内部drain资源缺失时跳过等待并返回成功。

## 调用示例

```cpp
// thread为控制面传入的有效ThreadHandle资源句柄；channel为控制面传入的有效ChannelHandle资源句柄。
int32_t ret = asccomm_channel_drain_on_thread(thread, channel);
```
