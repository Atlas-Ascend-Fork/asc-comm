# ccu_variable_handle

## 功能说明

头文件为：`#include <ccu/asc_ccu.h>`

`ccu_variable_handle`是CCU variable相关的句柄类型（`uint64_t`）。同一类型在不同场景下有两种用法，不能混用：

| 用途 | 来源 | 用法 |
| --- | --- | --- |
| 预约句柄 | Host侧调用`asccomm_ccu_variable_reserved`从CCU实例资源池预约一段物理标量寄存器后返回 | 传给`asccomm_ccu_variable_get_addr`查询预约段内资源的地址，或经`kernelArg`传入Kernel，供[variable](../device/resource_allocation_operation/variable.md)的预约构造、[array\<variable\>](../device/resource_allocation_operation/array.md)的预约构造绑定 |
| Kernel内虚拟句柄 | Kernel函数体内`variable`默认构造等资源创建接口产生 | 作为`variable`的`handle`成员，仅随`variable`对象参与同一Kernel的数据面接口；翻译完成后句柄即失效 |

## 类型定义

`ccu_variable_handle`在`ccu/ccu_api/ccu_types.h`中定义：

```c
typedef uint64_t ccu_variable_handle;
```

## 约束说明

- 不能把预约句柄当作`variable`的`handle`成员使用，也不能把Kernel内虚拟句柄传给`asccomm_ccu_variable_reserved`/`asccomm_ccu_variable_get_addr`——两种句柄的编号空间相互独立：预约构造在Kernel内会生成新的虚拟句柄（非预约句柄本身），预约句柄仅在Host侧资源分配/地址查询接口与Kernel内的预约构造中使用，不可当作`handle`成员使用。
- 不应在Kernel之外保存或比较`handle`值，翻译完成后句柄即失效。

## 调用示例

```cpp
// Host侧：预约标量寄存器并传递句柄
// 为简化示例，省略错误处理；insHandle、ccuDieId为用户上下文中的已有变量
// （分别来自CCU实例查询与die编号获取）
ccu_variable_handle varHandle = 0;
asccomm_ccu_variable_reserved(insHandle, ccuDieId, 10, &varHandle);
uint64_t va = 0;
asccomm_ccu_variable_get_addr(varHandle, 0, &va);  // 查询预约段内第0个资源的地址
// 把预约句柄和个数放进kernelArg传入kernel
struct my_kernel_arg {
    ccu_variable_handle var_handle;
    uint32_t var_num;
};
auto kernelArg = std::make_shared<my_kernel_arg>();
kernelArg->var_handle = varHandle;
kernelArg->var_num = 10;
```

```cpp
// Kernel侧：用预约句柄绑定预约段内资源（为简化示例，省略错误处理）
ccu_result my_kernel(ccu_kernel_arg arg) {
    auto* args = static_cast<my_kernel_arg*>(arg);

    ccu::array<ccu::variable> vars(args->var_handle, args->var_num);  // array<variable> 预约构造
    ccu::variable reservedVar(args->var_handle, 5);                   // 单对象预约构造，绑定第5个资源
    return ccu_result::CCU_SUCCESS;
}
```
