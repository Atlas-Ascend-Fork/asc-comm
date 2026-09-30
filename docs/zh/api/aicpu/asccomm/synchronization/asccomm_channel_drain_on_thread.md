# asccomm_channel_drain_on_thread

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

等待指定通道上已提交的通信操作完成。UB类协议通道会通过内部完成通知等待；RoCE通道当前不执行额外等待，直接返回成功。

本接口用于等待调用前已经提交的任务完成，不为调用后提交的任务增加保序约束。若需要保证屏障前后的通道读写操作顺序，请调用[asccomm_channel_fence_on_thread](asccomm_channel_fence_on_thread.md)。

## 函数原型

```cpp
int32_t asccomm_channel_drain_on_thread(ThreadHandle thread, ChannelHandle channel)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄，为控制面构造并随AICPU Kernel参数下发的有效Channel资源句柄，当前实现要求该参数不能为0。<br>ChannelHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- RoCE通道调用本接口时直接返回成功，不执行额外等待。
- UB类协议通道通过内部完成通知等待。
- UB通道的内部drain资源缺失时，当前实现会跳过等待并返回成功。

## 调用示例

```cpp
// RoCE通道当前直接返回成功；UB类协议通道通过内部完成通知等待。
int32_t ret = asccomm_channel_drain_on_thread(thread, channel);
```
