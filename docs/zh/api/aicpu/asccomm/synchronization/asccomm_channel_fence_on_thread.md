# asccomm_channel_fence_on_thread

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
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

在指定通信线程和通信通道上插入内存屏障操作，确保屏障前的通道读写操作在屏障后的通道读写操作之前完成。

## 函数原型

```cpp
int32_t asccomm_channel_fence_on_thread(ThreadHandle thread, ChannelHandle channel)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄，为控制面构造并随AICPU Kernel参数下发的有效Channel资源句柄，当前实现要求该参数不能为0。<br>ChannelHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- 本接口只设置Channel资源的fence标志，实际保序效果在后续提交通信操作时生效。
- Channel资源必须包含有效的SQ上下文。

## 调用示例

```cpp
// 设置通道fence标志，使后续提交的操作按fence语义保序。
int32_t ret = asccomm_channel_fence_on_thread(thread, channel);
```
