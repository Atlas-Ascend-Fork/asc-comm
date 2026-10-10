# write_reduce

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

将本端数据异步写入对端片上内存，同时按`op_type`指定的算子与对端现有数据进行归约`remote = reduce(remote, local, op_type)`，硬件完成时自动将`event`中`mask`对应的标志位设置为1。

具体支持的数据通路为：

- 本端片上内存 -> 对端片上内存

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
CcuResult write_reduce(ChannelHandle ch, remote_addr remote, local_addr local,
                      variable len, HcclDataType data_type, HcclReduceOp op_type,
                      event event, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| ch | 输入 | 跨rank通道句柄。 |
| remote | 输入 | 对端片上内存目标地址。对端内存在调用前须已写入有效初值，由对端kernel负责；硬件完成后该地址内容更新为归约结果。 |
| local | 输入 | 本端片上内存源地址。 |
| len | 输入 | 操作字节数，类型为`variable`，实际值在执行阶段取自对应的通用寄存器。 |
| data_type | 输入 | 数据类型，取值见`HcclDataType`枚举。 |
| op_type | 输入 | 归约算子，取值见`HcclReduceOp`枚举。当采用SUM操作时，低精度输入数据的求和结果会先升精度、再调整为与输入数据相同的精度。 |
| event | 输入 | 完成事件对象，硬件归约写完成时自动将`event`中`mask`对应的标志位设置为1，下游调用`event_wait(event, mask)`等待。 |
| mask | 输入 | 16位事件掩码，指定硬件完成时设置`event`的哪些标志位。默认值为`1`，即只置位bit0。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`remote`/`local`/`len`/`event`句柄未在当前kernel注册。 |

## 约束说明

- 对端`remote`内存须在调用前已写入有效初值，否则归约结果未定义。
- 参数顺序为目的端在前、源端在后：`write_reduce(ch, remote, local, ...)`，顺序写反会因类型不同在编译期报错。
- 同一kernel内所有channel须属于同一die，该约束由`<<<>>>`统一校验，违反时返回`CCU_E_PARA`。
- `data_type`仅支持`HCCL_DATA_TYPE_INT8`、`HCCL_DATA_TYPE_UINT8`、`HCCL_DATA_TYPE_INT16`、`HCCL_DATA_TYPE_UINT16`、`HCCL_DATA_TYPE_INT32`、`HCCL_DATA_TYPE_UINT32`、`HCCL_DATA_TYPE_FP16`、`HCCL_DATA_TYPE_BFP16`、`HCCL_DATA_TYPE_FP32`。
- `op_type`仅支持`HCCL_REDUCE_SUM`、`HCCL_REDUCE_MAX`、`HCCL_REDUCE_MIN`。
- `data_type`/`op_type`不在支持范围内时抛出异常（携带错误码），会被`<<<>>>`统一接住。
- 本接口为异步操作，须通过`event_wait(event, mask)`等待归约完成，方可保证对端数据可见。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 用户自定义的kernel入参结构体，Host侧建链后通过<<<>>>的kernelArg传入
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// 场景：将本端FP16数据写入对端并做SUM归约
CcuResult my_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体
    ChannelHandle ch = params->channel_handle;
    remote_addr remote;    // 对端须预先写入初值
    local_addr src;
    variable len;
    event evt;

    write_reduce(ch, remote, src, len, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
