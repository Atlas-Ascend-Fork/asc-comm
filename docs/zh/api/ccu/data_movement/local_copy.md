# local_copy

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

支持本端片上内存、本端CCU Buffer之间的异步数据拷贝，数据在传输过程中保持原始内容不变，硬件完成搬运时自动将`event`中`mask`对应的标志位设置为1。

具体支持的数据通路为：

- 本端片上内存 -> 本端片上内存
- 本端片上内存 -> 本端CCU Buffer
- 本端CCU Buffer -> 本端片上内存

> [!NOTE]说明
> 搬运完成时的自动置位等价于隐式的[event_record](../synchronization/event_record.md)，无需显式调用。

## 函数原型

- 本端片上内存 -> 本端片上内存

    ```cpp
    CcuResult local_copy(local_addr dst, local_addr src, variable len,
                        event event, uint16_t mask = 1);
    ```

- 本端片上内存 -> 本端CCU Buffer

    ```cpp
    CcuResult local_copy(ccu_buffer dst, local_addr src, variable len,
                        event event, uint16_t mask = 1);
    ```

- 本端CCU Buffer -> 本端片上内存

    ```cpp
    CcuResult local_copy(local_addr dst, ccu_buffer src, variable len,
                        event event, uint16_t mask = 1);
    ```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| dst | 输入 | 目的地址，数据写入其指向的内存。<br>&bull;本端片上内存 -> 本端片上内存、本端CCU Buffer -> 本端片上内存：类型为`local_addr`。<br>&bull;本端片上内存 -> 本端CCU Buffer：类型为`ccu_buffer`。 |
| src | 输入 | 源地址，<br>&bull;本端片上内存 -> 本端片上内存、本端片上内存 -> 本端CCU Buffer：类型为`local_addr`。<br>&bull;本端CCU Buffer -> 本端片上内存：类型为`ccu_buffer`。 |
| len | 输入 | 拷贝字节数，类型为`variable`，实际值在执行阶段取自对应的通用寄存器。 |
| event | 输入 | 完成事件对象，硬件搬运完成时自动将`event`中`mask`对应的标志位设置为1，下游调用`event_wait(event, mask)`等待。 |
| mask | 输入 | 16位事件掩码，指定硬件完成时设置`event`的哪些标志位。默认值为`1`，即只置位bit0。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`dst`/`src`/`len`/`event`句柄未在当前kernel注册。 |

> [!NOTE]说明
> 本接口不会返回`CCU_E_PARA`。`len`超过`ccu_buffer`单片4096字节时不会报错（详见[约束说明](#约束说明)）。

## 约束说明

- `dst`与`src`的地址区间不可重叠，重叠时硬件会发生未定义行为。
- 源或目标为`ccu_buffer`时，`len`不可超过`ccu_buffer`单片大小（4096字节）；该上限须由调用方自行保证，超出时硬件会发生未定义行为。
- 本接口为异步操作，须通过`event_wait(event, mask)`等待搬运完成后再访问目标内存。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 场景1：本端片上内存→本端片上内存拷贝
CcuResult my_kernel(ccu_kernel_arg arg) {
    local_addr src, dst;
    variable len;
    event evt;

    local_copy(dst, src, len, evt);    // mask默认0x1
    event_wait(evt);
    return CCU_SUCCESS;
}

// 场景2：本端片上内存→本端CCU Buffer拷贝
CcuResult my_kernel2(ccu_kernel_arg arg) {
    ccu_buffer buf;
    local_addr src;
    variable len;
    event evt;

    local_copy(buf, src, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}

// 场景3：本端CCU Buffer→本端片上内存拷贝
CcuResult my_kernel3(ccu_kernel_arg arg) {
    local_addr dst;
    ccu_buffer buf;
    variable len;
    event evt;

    local_copy(dst, buf, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
