# call_func

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

函数块的调用接口，与[func](func.md)配合使用。`call_func<obj>(args...)`首次调用时执行`obj`的lambda，将逻辑合成为一份函数块并追加一条跳转指令；对同一`obj`的后续调用只追加跳转指令，复用已合成的函数块。函数块的定义方式参见[func](func.md)。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {

// obj须为namespace作用域或static存储的ccu::func对象，args中每个参数为ccu::variable
template <func &obj, typename... args>
CcuResult call_func(args... args_);

} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| obj | 输入 | 模板参数，被调用的`ccu::func`对象，须为namespace作用域或static存储的对象。 |
| args... | 输入 | 实参列表，个数须与`obj`的lambda形参个数一致，每个实参类型为`ccu::variable`，调用时绑定到对应的lambda形参。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PARA` | 实参个数与`obj`的lambda形参个数不一致。 |
| 其他 | 跳转指令下发失败时返回的对应错误码。 |

> [!NOTE]说明
> 其余失败情形会抛出异常（携带错误码），不通过返回值反馈，如在硬件Loop的循环体内调用、嵌套调用、在注册阶段之外调用等，详见[约束说明](#约束说明)。

## 约束说明

- `obj`必须为namespace作用域或static存储的`ccu::func`对象，不能是函数局部变量：`call_func`以`func`对象引用作模板参数，这是C++语言要求，违反时编译期直接报错。
- 不可在硬件Loop的循环体内调用，否则抛出异常（错误码`CCU_E_INTERNAL`）。
- 不可嵌套调用：lambda内不可再调用`call_func`，违反时抛出异常（错误码`CCU_E_INTERNAL`）。
- 首次调用时合成函数块，对同一`obj`的后续调用复用已有函数块；不同的`ccu::func`对象即使lambda内容相同，也各自合成独立的函数块。
- 抛出的异常会被`<<<>>>`统一接住，对外返回`CCU_E_INTERNAL`，调用方无须自行捕获。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 声明为namespace作用域的static对象，满足模板参数的链接性要求
static func add_dbl([](variable x) {
    variable tmp;
    tmp = x + x;   // 对x翻倍
});

CcuResult my_kernel(ccu_kernel_arg arg) {
    variable a, b;
    a = 2;
    b = 5;

    // 首次调用：合成函数块，并追加一条跳转指令
    CcuResult ret = call_func<add_dbl>(a);
    if (ret != CCU_SUCCESS) { return ret; }

    // 第二次调用：仅追加跳转指令，复用已合成的函数块
    return call_func<add_dbl>(b);
}
```
