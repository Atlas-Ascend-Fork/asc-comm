# asccomm_write_reduce_on_thread

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

在指定Channel上发起归约写：将本端`src`中的`count`个`dataType`类型元素写入远端`dst`，并与`dst`中的对应元素按`reduceOp`计算，结果写回`dst`。接口调用方为`src`所在节点。

## 函数原型

```cpp
int32_t asccomm_write_reduce_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, AsccommDataType dataType, AsccommReduceOp reduceOp)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | -------------------------------------- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，包含`"aicpu/hcomm/asccomm_primitives.h"`后即可使用。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，包含`"aicpu/hcomm/asccomm_primitives.h"`后即可使用。 |
| dst | 输出 | 远端目标地址，用于保存归约结果。 |
| src | 输入 | 本端源地址，提供待写入的数据。 |
| count | 输入 | 元素个数。 |
| dataType | 输入 | 数据类型。类型为`AsccommDataType`，定义见`"aicpu/hcomm/asccomm_res_defs.h"`。<br>当前仅Ascend 950PR&950DT系列产品支持本接口，支持的数据类型见[dataType说明](#datatype说明)。|
| reduceOp | 输入 | 归约操作类型。类型为`AsccommReduceOp`，定义见`"aicpu/hcomm/asccomm_res_defs.h"`。<br>支持矩阵见[reduceOp说明](#reduceop说明)。 |

### dataType说明

仅Ascend 950PR&950DT系列产品支持本接口，支持的数据类型：int8、int16、int32、uint8、uint16、uint32、fp16、fp32、bfp16。

### reduceOp说明

`AsccommReduceOp`包含`ASCCOMM_REDUCE_SUM`、`ASCCOMM_REDUCE_PROD`、`ASCCOMM_REDUCE_MAX`、`ASCCOMM_REDUCE_MIN`和`ASCCOMM_REDUCE_RESERVED`。当前各通道协议的支持情况如下。

| reduceOp | UB类协议通道 | RoCE通道 |
| --- | --- | --- |
| `ASCCOMM_REDUCE_SUM` | 支持归约写。 | 支持归约写。 |
| `ASCCOMM_REDUCE_PROD` | 不支持，当前实现返回`ASCCOMM_E_INTERNAL`。 | 不支持，返回`ASCCOMM_E_NOT_SUPPORT`。 |
| `ASCCOMM_REDUCE_MAX` | 支持归约写。 | 支持归约写。 |
| `ASCCOMM_REDUCE_MIN` | 支持归约写。 | 支持归约写。 |
| `ASCCOMM_REDUCE_RESERVED` | 非法保留值，返回`ASCCOMM_E_PARA`。 | 非法保留值，返回`ASCCOMM_E_PARA`。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 归约写请求处理成功。UB通道表示归约写任务已提交；RoCE通道在返回前检查归约写操作完成结果。 |
| `ASCCOMM_E_PTR` | `dst`或`src`为空、Thread或Channel句柄为0，或所需资源地址为空。 |
| `ASCCOMM_E_PARA` | `dataType`或`reduceOp`非法，`count * sizeof(dataType)`溢出，或Thread/Channel资源及内存保护参数不合法。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel协议不受支持，或RoCE inline归约不支持指定的数据类型或归约操作。 |
| `ASCCOMM_E_TIMEOUT` | 通信任务提交队列或RoCE队列等待超时，或RoCE完成事件轮询超时。 |
| `ASCCOMM_E_REMOTE` | RoCE完成事件报告远端通信错误。 |
| `ASCCOMM_E_INTERNAL` | UB归约任务编码、任务提交或其他内部操作异常。 |

## 约束说明

- 本接口同时支持UB类协议和RoCE协议通道。
- `src`和`dst`应使用该Channel对应的本端和远端通信内存地址。
- `dst`地址范围必须落在远端已注册的通信内存范围内。
- `count * sizeof(dataType)`溢出时返回`ASCCOMM_E_PARA`。

## 调用示例

```cpp
// thread为控制面传入的有效ThreadHandle资源句柄；channel为控制面传入的有效ChannelHandle资源句柄。
// src为本端通信内存地址；dst为远端通信内存地址。
uint64_t count = 256;
int32_t ret = asccomm_write_reduce_on_thread(
    thread, channel, dst, src, count, ASCCOMM_DATA_TYPE_FP32, ASCCOMM_REDUCE_SUM);
```
