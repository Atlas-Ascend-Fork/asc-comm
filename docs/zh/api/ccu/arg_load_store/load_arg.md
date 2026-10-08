# load_arg

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

`load_arg`在kernel函数体内调用，将host下发的参数加载到`variable`。host每次通过`<<<>>>`下发Kernel时，通过`taskArgs`数组传入参数；Kernel开始执行前，由硬件将`taskArgs[arg_id]`的值写入目标`variable`对应的寄存器，作为其初值。

本接口在注册阶段仅记录声明，不执行加载，实际加载由硬件在每次下发时完成。由此，同一份已注册的Kernel每次下发可携带不同参数，无须重复注册。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
CcuResult load_arg(variable v, uint32_t arg_id);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| v | 输入 | 目标`variable`对象，接收`taskArgs[arg_id]`注入的值作为初值。 |
| arg_id | 输入 | `taskArgs`数组的索引。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`v`句柄未在当前kernel注册。 |

> [!NOTE]说明
> 本接口不会返回`CCU_E_PARA`。`arg_id`相关的约束错误不在本接口处返回，而是由`<<<>>>`统一校验后返回`CCU_E_INTERNAL`，详见[约束说明](#约束说明)。

## 约束说明

- 同一kernel内所有`load_arg`调用的`arg_id`须从0连续编号（0、1、2、…），不可重复，且个数须与`<<<>>>`的`argNum`严格相等。加载指令按`arg_id`升序对应到任务参数槽位，跳号、乱序或重复会使参数无法与槽位正确对应——以上均由`<<<>>>`统一校验，违反时返回`CCU_E_INTERNAL`。
- `argNum`表示元素个数，而非字节数。例如`arg_id`为0、1、2的3个`load_arg`调用，`argNum`须传`3`，`taskArgs`须有至少3个`uint64_t`元素。
- `arg_id`取值范围为0~12，因为CCU任务描述符最多携带13个参数。
- `load_arg`只负责给`v`设初值：若kernel函数体内又对同一个`v`赋值，赋值会覆盖注入的初值。混用时须先把注入值读走、再重新赋值。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 场景：注册时声明循环次数n和起始偏移offset由host在Launch时传入
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable n, offset;

    load_arg(n, 0);        // 每次下发时taskArgs[0] → n
    load_arg(offset, 1);   // 每次下发时taskArgs[1] → offset

    // 后续使用n和offset进行循环和地址计算
    // ...
    return CCU_SUCCESS;
}

// Host侧对应的<<<>>>（示意）：
// uint64_t taskArgs[] = {100, 4096};   // n=100轮，offset=4096字节
// kernel_name<<<schd, insHandle, stream>>>(taskArgs[0], taskArgs[1]);
```
