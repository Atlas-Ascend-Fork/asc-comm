# asccomm_channel_fence_on_thread

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

在指定Channel上设置屏障标志，用于保证屏障前的通道读写操作先于屏障后的通道读写操作完成。

## 函数原型

```cpp
int32_t asccomm_channel_fence_on_thread(ThreadHandle thread, ChannelHandle channel)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 屏障标志设置成功。 |
| `ASCCOMM_E_PTR` | Thread或Channel句柄为0，或Thread资源指针为空。 |
| `ASCCOMM_E_PARA` | Thread/Channel资源ABI、RTSQ或Channel SQ上下文参数不合法。 |
| `ASCCOMM_E_INTERNAL` | 内部执行异常（异常兜底）。 |

## 约束说明

- 本接口只设置Channel的fence标志；该标志由下一次相关传输任务消费，消费后自动清零。
- Channel资源必须已创建SQ上下文，屏障标志写入该上下文；缺少SQ上下文时返回`ASCCOMM_E_PARA`。

## 调用示例

```cpp
// 设置通道fence标志，使后续提交的操作按fence语义保序。
int32_t ret = asccomm_channel_fence_on_thread(thread, channel);
```
