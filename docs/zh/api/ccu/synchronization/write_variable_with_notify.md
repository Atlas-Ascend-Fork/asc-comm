# write_variable_with_notify

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

在CCU kernel内将“写对端variable值”与“触发对端notify信号”合并为单条原子操作，硬件保证“先写值后发通知”的完成顺序。消费侧通过[notify_wait](notify_wait.md)等待信号，再通过`AscendC::ccu::get_res_by_channel<variable>(channel, remote_var_idx)`读取写入的新值。

> [!CAUTION]注意
> 不可将本接口拆分为独立的“写variable”与“notify_record”两步调用。硬件层不保证两条独立操作的到达顺序，消费侧收到notify信号后读取对端variable时，存在读到旧值的风险。仅本接口能从硬件层保证顺序。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
CcuResult write_variable_with_notify(ChannelHandle channel, variable var,
                                    uint32_t remote_var_idx, uint32_t remote_notify_idx,
                                    uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| channel | 输入 | 通信通道句柄，通过建链流程获取的通道资源。类型为[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)。 |
| var | 输入 | 本端`variable`对象，当前值将被写入对端。 |
| remote_var_idx | 输入 | 对端variable槽位索引。消费侧通过`get_res_by_channel<variable>(channel, remote_var_idx)`引用同一槽位。 |
| remote_notify_idx | 输入 | 对端notify槽位索引。须与消费侧`notify_wait`的`local_notify_idx`使用相同数值。 |
| mask | 输入 | 16位事件掩码，指定要设置的对端notify标志位。默认值为`1`，即bit0。须与消费侧`notify_wait`的`mask`一致。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_NOT_FOUND` | `var`对应的variable句柄未在当前kernel注册。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |

## 约束说明

- 不可在硬件loop的循环体内调用。
- 同一kernel内所有channel须属于同一die，不同die的channel不可混入同一kernel。该约束由`<<<>>>`统一校验，违反时返回`CCU_E_PARA`。
- `remote_var_idx`与`remote_notify_idx`均为channel级别的槽位编号，由两端kernel各自传入：框架不分配编号，也不校验两侧是否一致，须由开发者保证两端使用相同编号，若不一致则消费侧将永久阻塞。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 用户自定义的kernel入参结构体，Host侧建链后通过<<<>>>的kernelArg传入
struct my_kernel_arg {
    ChannelHandle channel;
};

// 场景：生产者将进度值发给消费者，消费者收到通知后读取
// 生产者CCU kernel函数体内（die A）
CcuResult producer_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体
    variable progress;
    progress = 100;  // 赋立即数100

    // 原子操作：写对端变量槽位2 + 通知对端notify槽位3的bit0
    write_variable_with_notify(params->channel, progress,
                               /*remote_var_idx=*/2, /*remote_notify_idx=*/3, 0x1);
    return CCU_SUCCESS;
}

// 消费者CCU kernel函数体内（die B）
CcuResult consumer_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体
    // 等待来自die A的信号
    notify_wait(params->channel, /*local_notify_idx=*/3, 0x1);

    // 安全读取die A写入的值（保证已写入完成）
    variable received = get_res_by_channel<variable>(params->channel, /*var_idx=*/2);
    return CCU_SUCCESS;
}
```
