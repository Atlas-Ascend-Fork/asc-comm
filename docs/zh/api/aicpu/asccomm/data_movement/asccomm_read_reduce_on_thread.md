# asccomm_read_reduce_on_thread

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

头文件路径：`"aicpu/hcomm/asccomm_primitives.h"`。

在指定Channel上发起归约读：从远端`src`读取`count`个`dataType`类型的元素，与本端`dst`中的对应元素按`reduceOp`计算，并将结果写回`dst`。接口调用方为`dst`所在节点。

## 函数原型

```cpp
int32_t asccomm_read_reduce_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| dst | 输出 | 本端目标地址，用于保存归约结果。 |
| src | 输入 | 远端源地址，提供待读取的数据。 |
| count | 输入 | 元素个数。 |
| dataType | 输入 | 数据类型。<br>HcommDataType类型的定义请参见`"aicpu/hcomm/hcomm_res_defs.h"`。<br>不同产品型号下支持的数据类型不同，详细请参见[dataType说明](#datatype说明)。|
| reduceOp | 输入 | 归约操作类型，支持：sum、max、min。<br>HcommReduceOp类型的定义请参见`"aicpu/hcomm/hcomm_res_defs.h"`。 |

### dataType说明

<!-- npu="950" id7 -->
- 针对Ascend 950PR/Ascend 950DT系列产品，支持的数据类型：int8、int16、int32、uint8、uint16、uint32、fp16、fp32、bfp16。
<!-- end id7 -->
<!-- npu="A3" id8 -->
- 针对Atlas A3系列产品，支持数据类型：int8、int16、int32、float16、float32、bfp16。
<!-- end id8 -->
<!-- npu="910b" id9 -->
- 针对Atlas A2系列产品，支持数据类型：int8、int16、int32、float16、float32、bfp16。
<!-- end id9 -->

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | UB通道归约读任务提交成功；RoCE通道直接返回成功，但不执行归约读，`dst`不会因本次调用更新。 |
| `ASCCOMM_E_PTR` | `dst`或`src`为空、Thread或Channel句柄为0，或UB通道所需资源地址为空。 |
| `ASCCOMM_E_PARA` | `dataType`或`reduceOp`非法，`count * sizeof(dataType)`溢出，或Thread/Channel资源及内存保护参数不合法。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel协议既不是当前支持的UB类协议，也不是RoCE协议。 |
| `ASCCOMM_E_TIMEOUT` | UB任务提交时RTSQ空间不足且等待超时。 |
| `ASCCOMM_E_INTERNAL` | UB归约任务编码、任务提交或其他内部操作异常。 |

## 约束说明

- UB类协议通道支持归约读操作。
- RoCE通道当前不执行归约读操作，直接返回成功；调用成功不代表目的内存被更新。
- `dst`和`src`应使用该Channel对应的本端和远端通信内存地址；`src`地址范围必须落在远端已注册的通信内存范围内。
- `count * sizeof(dataType)`溢出时返回`ASCCOMM_E_PARA`。

## 调用示例

```cpp
// thread、channel、dst和src已由前置流程准备；src为远端通信内存地址，dst为本端目标地址。
uint64_t count = 256;
int32_t ret = asccomm_read_reduce_on_thread(
    thread, channel, dst, src, count, HCOMM_DATA_TYPE_FP32, HCOMM_REDUCE_SUM);
```
