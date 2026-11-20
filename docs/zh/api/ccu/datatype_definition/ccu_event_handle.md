# ccu_event_handle

## 功能说明

头文件为：`#include <ccu/asc_ccu.h>`

`ccu_event_handle`是CCU event相关的句柄类型（`uint64_t`）。同一类型在不同场景下有两种用法，不能混用：

| 用途 | 来源 | 用法 |
| --- | --- | --- |
| 预约句柄 | Host侧调用`asccomm_ccu_event_reserved`从CCU实例资源池预约一段物理event槽位后返回 | 传给`asccomm_ccu_event_get_addr`查询预约段内资源的地址，或经`kernelArg`传入Kernel，供[event](../device/resource_allocation_operation/event.md)的预约构造、[array\<event\>](../device/resource_allocation_operation/array.md)的预约构造绑定 |
| Kernel内虚拟句柄 | Kernel函数体内`event`默认构造等资源创建接口产生 | 作为`event`的`handle`成员，仅随`event`对象参与同一Kernel的数据面同步与搬运接口；翻译完成后句柄即失效 |

## 类型定义

`ccu_event_handle`在`ccu/ccu_api/ccu_types.h`中定义：

```c
typedef uint64_t ccu_event_handle;
```

## 约束说明

- 不能把预约句柄当作`event`的`handle`成员使用，也不能把Kernel内虚拟句柄传给`asccomm_ccu_event_reserved`/`asccomm_ccu_event_get_addr`——两种句柄的编号空间相互独立：预约构造在Kernel内会生成新的虚拟句柄（非预约句柄本身），预约句柄仅在Host侧资源分配/地址查询接口与Kernel内的预约构造中使用，不可当作`handle`成员使用。
- 不应在Kernel之外保存或比较`handle`值，翻译完成后句柄即失效。

## 调用示例

```cpp
// Host侧：预约event槽位并传递句柄
// 为简化示例，省略错误处理；insHandle、ccuDieId为用户上下文中的已有变量
// （分别来自CCU实例查询与die编号获取）
ccu_event_handle eventHandle = 0;
asccomm_ccu_event_reserved(insHandle, ccuDieId, 2, &eventHandle);
uint64_t va = 0;
asccomm_ccu_event_get_addr(eventHandle, 0, &va);  // 查询预约段内第0个槽位的地址
// 把预约句柄和个数放进kernelArg传入kernel
struct my_kernel_arg {
    ccu_event_handle event_handle;
    uint32_t event_num;
};
auto kernelArg = std::make_shared<my_kernel_arg>();
kernelArg->event_handle = eventHandle;
kernelArg->event_num = 2;
```

```cpp
// Kernel侧：用预约句柄绑定预约段内槽位（为简化示例，省略错误处理）
ccu_result my_kernel(ccu_kernel_arg arg) {
    auto* args = static_cast<my_kernel_arg*>(arg);

    ccu::array<ccu::event> events(args->event_handle, args->event_num);  // array<event> 预约构造
    ccu::event reservedEvent(args->event_handle, 1);                     // 单对象预约构造，绑定第1个槽位
    return ccu_result::CCU_SUCCESS;
}
```
