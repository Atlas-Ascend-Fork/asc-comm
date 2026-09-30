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

头文件路径：`"asccomm/asccomm_primitives.h"`。

从channel上的指定内存读数据，从src中读取长度为count\*sizeof\(dataType\)的内存数据，与dst所指向的相同长度的内存数据进行reduceOp操作，并将结果输出到dst中。接口调用方为dst所在节点。

## 函数原型

```cpp
int32_t asccomm_read_reduce_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄，为控制面构造并随AICPU Kernel参数下发的有效Channel资源句柄，当前实现要求该参数不能为0。<br>ChannelHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| dst | 输出 | 目的内存地址，通道已注册RMA内存中的地址。 |
| src | 输入 | 源内存地址，通道已注册RMA内存中的地址。 |
| count | 输入 | 元素个数。 |
| dataType | 输入 | 数据类型。<br>HcommDataType类型的定义请参见`"asccomm/hcomm_res_defs.h"`。<br>不同产品型号下支持的数据类型不同，详细请参见[dataType说明](#dataType说明)。|
| reduceOp | 输入 | 归约操作类型，支持：sum、max、min。<br>HcommReduceOp类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |

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

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- UB类协议通道支持归约读操作。
- RoCE通道当前不执行归约读操作，直接返回成功；调用成功不代表目的内存被更新。
- 除RoCE外的其他非UB协议返回`ASCCOMM_E_NOT_SUPPORT`。
- `count * sizeof(dataType)`溢出时返回`ASCCOMM_E_PARA`。

## 调用示例

```cpp
int32_t ret = asccomm_read_reduce_on_thread(
    thread, channel, dst, src, count, HCOMM_DATA_TYPE_FP32, HCOMM_REDUCE_SUM);
```
