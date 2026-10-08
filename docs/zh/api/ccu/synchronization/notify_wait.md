# notify_wait

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

阻塞等待本端同步寄存器中`mask`对应的notify标志位，直到其全部为1后继续执行。该标志位由对端die的[notify_record](notify_record.md)通过channel置位。

> [!NOTE]说明
> 适用于跨die场景：同device跨die、同节点跨device、跨节点均可，信号经channel送达对端。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
CcuResult notify_wait(ChannelHandle channel, uint32_t local_notify_idx, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| channel | 输入 | 通信通道句柄，类型为[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)。 |
| local_notify_idx | 输入 | 本端notify槽位索引，取值范围为0~7。 |
| mask | 输入 | 16位事件掩码，指定要等待的本端notify标志位。默认值为`1`，即bit0。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |

## 约束说明

- `notify_wait`可在硬件loop的循环体内调用。
- 同一kernel内所有channel须属于同一die，不同die的channel不可混入同一kernel。该约束由`<<<>>>`统一校验，违反时返回`CCU_E_PARA`。
- `notify_record`的`remote_notify_idx`和`notify_wait`的`local_notify_idx`数值相同。
- `notify_record`和`notify_wait`的`mask`相同。
- 单个[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)可用的notify槽位最多8个，因此`local_notify_idx`的取值范围为0~7。
- 调用本接口前，应先调用`notify_record`将对端标志位置位，否则将永久阻塞导致硬件级死锁。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 用户自定义的kernel入参结构体，Host侧建链后通过<<<>>>的kernelArg传入
struct my_kernel_arg {
    ChannelHandle channel;
};

// 场景：die B等待die A完成写操作后再读取数据
// die B的CCU kernel函数体内
CcuResult receiver_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体
    // 等待来自die A的信号，本端notify槽位3的bit0
    // 对应die A侧notify_record(channel, /*remote_notify_idx=*/3, 0x1)
    notify_wait(params->channel, /*local_notify_idx=*/3, 0x1);

    // 此后可安全读取die A写入的数据
    remote_addr remote;
    local_addr dst;
    variable len;
    event evt;
    read(params->channel, dst, remote, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
