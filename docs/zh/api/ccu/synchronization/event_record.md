# event_record

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

将本端同步寄存器中`mask`对应的event标志位显式设置为1，表示当前操作或阶段已完成，供同kernel内的[event_wait](event_wait.md)读取完成信号。

> [!NOTE]说明
> 绝大多数场景下无需显式调用本接口，仅当某位置未调用搬运接口时需要显式调用，如数据量为0而跳过搬运、或分支代码段内只有计算，此时本端`event`中对应的位不会被自动置位，须调用本接口补置，否则下游的`event_wait`将永久阻塞。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
CcuResult event_record(event e, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| e | 输入 | 本地事件对象，对应同步寄存器中的一个event槽位。默认构造时自动申请虚拟句柄，物理资源在注册阶段统一分配。 |
| mask | 输入 | 16位事件掩码，指定要设置的`event`标志位。默认值为`1`，即bit0。同一个`event`对象的不同标志位相互独立，可分别用于多组`event_record`与`event_wait`的配对。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`e`句柄未在当前kernel注册。 |
| `CCU_E_NOT_SUPPORT` | 不支持在硬件loop的循环体内调用。 |

## 约束说明

- 不可在硬件loop的循环体内调用，否则返回`CCU_E_NOT_SUPPORT`。循环体会被复制为多份并行执行，每份都会执行一次`event_record`，置位语义不再明确。
- 置位后须有[event_wait](event_wait.md)消费该信号，否则该次置位不被使用。
- `event_record`和`event_wait`的`mask`必须一致，否则`event_wait`永远等不到信号。

## 调用示例

```cpp
using namespace AscendC::ccu;

// CCU kernel函数体内
CcuResult my_kernel(ccu_kernel_arg arg) {
    event evt;

    // 执行某些操作后，手动标记bit0完成
    event_record(evt, 0x1);

    // 下游等待bit0被置位
    event_wait(evt, 0x1);
    return CCU_SUCCESS;
}
```
