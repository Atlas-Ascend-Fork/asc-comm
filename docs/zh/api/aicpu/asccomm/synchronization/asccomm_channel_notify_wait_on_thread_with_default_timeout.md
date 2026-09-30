# asccomm_channel_notify_wait_on_thread_with_default_timeout

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

等待同步信号，阻塞等待指定Channel上的Notify完成。**使用通过[asccomm_set_notify_wait_timeout](asccomm_set_notify_wait_timeout.md)设置的默认超时时间**，无需手动传入超时参数。

## 函数原型

```cpp
int32_t asccomm_channel_notify_wait_on_thread_with_default_timeout(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄，为控制面构造并随AICPU Kernel参数下发的有效Channel资源句柄，当前实现要求该参数不能为0。<br>ChannelHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| localNotifyIdx | 输入 | 本地Notify索引。<br>取值范围：［0，可用于等待的本地Notify数量）。 |

<!-- npu="950" id7 -->
**补充说明**：

**thread参数**：当前实现要求“thread”参数为有效的Thread资源句柄，不能传入0。
<!-- end id7 -->

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- `localNotifyIdx`必须小于Channel资源中可用于等待的本地Notify数量。
- 默认超时时间通过[asccomm_set_notify_wait_timeout](asccomm_set_notify_wait_timeout.md)设置；未设置时为1836秒。
- 默认超时时间为0表示永久等待，大于0表示具体等待秒数。

## 调用示例

```cpp
uint32_t localNotifyIdx = 0;
// 使用asccomm_set_notify_wait_timeout设置的默认超时时间等待。
int32_t ret = asccomm_channel_notify_wait_on_thread_with_default_timeout(thread, channel, localNotifyIdx);
```
