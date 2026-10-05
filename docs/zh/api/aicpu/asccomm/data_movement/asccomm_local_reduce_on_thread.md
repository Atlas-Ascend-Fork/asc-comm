# asccomm_local_reduce_on_thread

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

提供本地归约功能：将`src`中的`count`个`dataType`类型元素与`dst`中的对应元素按`reduceOp`进行计算，并将结果写回`dst`。

## 函数原型

```cpp
int32_t asccomm_local_reduce_on_thread(ThreadHandle thread, void *dst, const void *src, uint64_t count, AsccommDataType dataType, AsccommReduceOp reduceOp)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| dst | 输出 | 目的地址，Device内存。 |
| src | 输入 | 源地址，Device内存。 |
| count | 输入 | 元素个数。`count`为0时不生成归约任务。 |
| dataType | 输入 | 数据类型。当前支持：int8、int16、int32、fp16、fp32、bfp16。 |
| reduceOp | 输入 | 归约操作类型。支持矩阵见[reduceOp说明](#reduceop说明)。 |

`AsccommDataType`和`AsccommReduceOp`定义于`"aicpu/hcomm/asccomm_res_defs.h"`。

### reduceOp说明

`AsccommReduceOp`包含`ASCCOMM_REDUCE_SUM`、`ASCCOMM_REDUCE_PROD`、`ASCCOMM_REDUCE_MAX`、`ASCCOMM_REDUCE_MIN`和`ASCCOMM_REDUCE_RESERVED`。本地SDMA归约的当前支持情况如下。

| reduceOp | 本地SDMA归约 | 不支持或非法时返回值 |
| --- | --- | --- |
| `ASCCOMM_REDUCE_SUM` | 支持 | 不适用。 |
| `ASCCOMM_REDUCE_PROD` | 不支持 | `ASCCOMM_E_NOT_SUPPORT`。 |
| `ASCCOMM_REDUCE_MAX` | 支持 | 不适用。 |
| `ASCCOMM_REDUCE_MIN` | 支持 | 不适用。 |
| `ASCCOMM_REDUCE_RESERVED` | 非法保留值 | `ASCCOMM_E_PARA`。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 归约任务已生成（`count`为0时不生成任务），不表示归约操作已经完成。 |
| `ASCCOMM_E_PTR` | Thread句柄为0、源地址或目的地址为空，或Thread内部任务缓存地址为空。 |
| `ASCCOMM_E_PARA` | 数据类型或归约操作非法、总数据长度计算溢出，或Thread/RTSQ参数不合法。 |
| `ASCCOMM_E_NOT_SUPPORT` | 当前本地SDMA归约不支持指定的归约操作或数据类型。 |
| `ASCCOMM_E_TIMEOUT` | RTSQ空间不足，等待队列空间超时。 |
| `ASCCOMM_E_INTERNAL` | 获取任务ID、查询或配置RTSQ、归约任务提交等内部操作失败。 |

## 约束说明

- dst和src必须为非NULL且可被AICPU直接访问的有效Device地址。

## 调用示例

```cpp
// thread、src和dst已由前置流程准备。
// thread为控制面传入的有效Thread资源句柄；src和dst为AICPU可直接访问的Device地址。
uint64_t count = 256;
int32_t ret = asccomm_local_reduce_on_thread(
    thread, dst, src, count, ASCCOMM_DATA_TYPE_FP32, ASCCOMM_REDUCE_SUM);
```
