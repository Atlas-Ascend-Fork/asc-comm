# asccomm_channel_notify_record_on_thread

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

向指定Channel的远端Notify发送同步信号，主要用于Channel两端同步等待场景。

## 函数原型

```cpp
int32_t asccomm_channel_notify_record_on_thread(ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| remoteNotifyIdx | 输入 | 远端Notify索引。取值范围为`[0, remoteNotifyNum)`，其中`remoteNotifyNum`为Channel资源中的远端Notify数量。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 通知任务处理成功。UB类协议通道表示任务已生成；RoCE通道在返回前检查通知写操作完成结果。 |
| `ASCCOMM_E_PTR` | Thread或Channel句柄为0，或通信资源、内存保护指针为空。 |
| `ASCCOMM_E_PARA` | Thread/Channel资源ABI、通信上下文、Notify或内存保护参数不合法。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel协议既不是当前支持的UB类协议，也不是RoCE协议。 |
| `ASCCOMM_E_TIMEOUT` | RTSQ或RoCE队列等待超时，或RoCE完成事件轮询超时。 |
| `ASCCOMM_E_REMOTE` | RoCE完成事件报告远端通信错误。 |
| `ASCCOMM_E_INTERNAL` | 任务生成、队列、Doorbell或RTSQ缓存操作等内部执行失败。 |

## 约束说明

- 当前实现支持UB类协议和RoCE协议通道；其他协议返回`ASCCOMM_E_NOT_SUPPORT`。
- `remoteNotifyIdx`必须小于通道远端Notify数量，并且对应的Notify资源类型和内存保护参数须正确。
- 通知任务提交时机与Thread资源的提交策略一致；普通模式可立即提交，批量模式或缓存满时按当前实现提交。
- 该接口需与[asccomm_channel_notify_wait_on_thread](asccomm_channel_notify_wait_on_thread.md)配合使用。

## 调用示例

```cpp
uint32_t remoteNotifyIdx = 0;
// thread和channel已由前置流程准备。
int32_t ret = asccomm_channel_notify_record_on_thread(thread, channel, remoteNotifyIdx);
```
