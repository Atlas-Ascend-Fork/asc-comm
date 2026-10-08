# event_wait

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

阻塞等待本端同步寄存器中`mask`对应的event标志位，直到其全部为1后继续执行，随后自动将这些标志位清零。该标志位由同kernel内的[event_record](event_record.md)或[数据搬运接口](../data_movement/README.md)置位。

> [!NOTE]说明
> `event_wait`每次等待成功后，对应的标志位会被自动清零。想再次等待同样的标志位，须先由生产侧重新置位。同一个`event`的不同标志位相互独立，各自置位、等待互不影响。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
CcuResult event_wait(event e, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| e | 输入 | 本地事件对象，对应同步寄存器中的一个event槽位。默认构造时自动申请虚拟句柄，物理资源在注册阶段统一分配。 |
| mask | 输入 | 16位事件掩码，指定要等待的`event`标志位。默认值为`1`，即bit0。支持多bit合并等待，例如`mask = 0x3`表示等待bit0和bit1全部置位。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`e`句柄未在当前kernel注册。 |

## 约束说明

- `event_wait`可在硬件loop的循环体内调用。
- 调用本接口前，对应的标志位须已被置位。可以是[数据搬运接口](../data_movement/README.md)在搬运完成时自动置位，也可以是显式调用[event_record](event_record.md)置位；否则本接口将永久阻塞，导致硬件级死锁。
- 本接口的`mask`须与生产侧传入的`mask`一致，否则永远等不到信号。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 场景：等待本端片上内存→片上内存拷贝完成后再继续
// CCU kernel函数体内
CcuResult my_kernel(ccu_kernel_arg arg) {
    local_addr src, dst;
    variable len;
    event evt;

    // 发起异步拷贝，硬件完成时自动将evt中mask对应的标志位设置为1
    local_copy(dst, src, len, evt, 0x1);

    // 阻塞等待拷贝完成
    event_wait(evt, 0x1);

    // 此后可安全使用dst指向的数据
    return CCU_SUCCESS;
}
```
