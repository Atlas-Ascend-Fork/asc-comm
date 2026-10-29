# read_reduce

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

头文件为：`#include "ccu/hcomm/ccu_primitives.hpp"`

通过已建链的[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)从对端片上内存异步读取数据，并与本端片上内存现有数据按指定算子合并（`local = reduce(local, fetch_from_remote, op_type)`），硬件完成时自动将`event`中`mask`对应的标志位设置为1。

## 函数原型

```cpp
CcuResult read_reduce(ChannelHandle ch, local_addr local, remote_addr remote,
                      variable len, HcclDataType data_type, HcclReduceOp op_type,
                      event event, uint16_t mask = 1);
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| ch | 输入 | 跨rank通道句柄。 |
| local | 输入/输出 | 本端片上内存目标地址，类型为`local_addr`，同时也是归约的累加端。调用前须写入有效初值；硬件完成后更新为归约结果。 |
| remote | 输入 | 对端源地址，类型为`remote_addr`。 |
| len | 输入 | 操作字节数，类型为`variable`，实际值在执行阶段取自对应的通用寄存器。 |
| data_type | 输入 | 数据类型，取值见`HcclDataType`枚举。仅支持以下6种：`HCCL_DATA_TYPE_UINT8`、`HCCL_DATA_TYPE_INT16`、`HCCL_DATA_TYPE_INT32`、`HCCL_DATA_TYPE_FP16`、`HCCL_DATA_TYPE_FP32`、`HCCL_DATA_TYPE_BFP16`；其他取值会被拒绝并抛出异常（携带错误码）。 |
| op_type | 输入 | 归约算子，取值见`HcclReduceOp`枚举。仅支持`HCCL_REDUCE_SUM`（求和）、`HCCL_REDUCE_MAX`（最大值）、`HCCL_REDUCE_MIN`（最小值）；`HCCL_REDUCE_PROD`不支持，传入会被拒绝并抛出异常（携带错误码）。当采用SUM操作时，低精度输入数据的求和结果会先升精度、再调整为与输入数据相同的精度。 |
| event | 输入 | 完成事件对象，硬件归约读完成时自动将`event`中`mask`对应的标志位设置为1，下游调用`event_wait(event, mask)`等待。 |
| mask | 输入 | 16位事件掩码，指定硬件完成时设置`event`的哪些标志位。默认值为`1`，即只置位bit0。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`local`/`remote`/`len`/`event`句柄未在当前kernel注册。 |

## 约束说明

- 参数顺序为目的端在前、源端在后：`read_reduce(ch, local, remote, ...)`，`local`同时也是归约的累加端。
- 同一kernel内所有[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)须属于同一die，不同die的channel不可混入同一kernel。die一致性由`<<<>>>`统一校验，不一致时返回`CCU_E_PARA`，而非本接口的返回值。
- `local`在调用前须已写入有效初值（如0或负无穷），否则归约结果未定义。
- 本接口为异步操作，须通过`event_wait(event, mask)`等待归约完成后再读取结果。

## 调用示例

```cpp
// 为简化示例，省略返回值接收与错误处理
using namespace AscendC::ccu;

// 用户自定义的kernel入参结构体，Host侧建链后通过<<<>>>的kernelArg传入
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// 场景：从对端片上内存读取并与本端数据做FP16 SUM归约
CcuResult my_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体
    ChannelHandle ch = params->channel_handle;
    local_addr dst;      // dst须预先写入初值（如0.0）
    remote_addr remote;
    variable len;
    event evt;

    read_reduce(ch, dst, remote, len, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
