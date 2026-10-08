# CCU_IF

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

在CCU kernel内开始一个软件条件分支块。条件在执行阶段由CCU硬件计算，条件满足时执行then代码块，不满足时跳转至if块出口。

## 函数原型

```cpp
CCU_IF(cond_expr) {
    // then分支代码块
}
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| cond_expr | 输入 | 条件表达式，类型为`AscendC::ccu::cond_expr`。通过`ccu::variable`的`operator==(uint64_t)`或`operator!=(uint64_t)`产生，例如`v == 0`或`v != limit`。 |

## 返回值说明

`CCU_IF`为预处理器宏，本身不返回[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)。宏展开后若发生内部错误（如label重复、句柄无效），可能产生`CCU_E_PARA`/`CCU_E_NOT_FOUND`。此时宏不抛异常、也不阻断注册：失败时代码块被跳过，注册仍然成功。错误不通过返回值反馈，调试时需留意。

## 约束说明

- `CCU_IF(cond_expr)`后须紧跟`{}`包裹的then代码块。
- `cond_expr`仅支持`==`和`!=`两种比较运算。
- 支持嵌套：`CCU_IF`代码块内可以再嵌套`CCU_IF`。
- 不可在硬件Loop的循环体内使用。虽然代码不会报错，但硬件会发生未定义行为。
- `cond_expr`中比较的立即数须为`uint64_t`类型，不支持`variable`与`variable`之间的比较。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 场景：根据执行阶段变量值选择不同的执行路径
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable flag;
    load_arg(flag, 0);   // 每次下发时从taskArgs[0]加载标志值

    CCU_IF(flag == 0) {
        // flag为0时执行此分支
        variable a, b;
        a = 10;
        b = 20;
    }

    return CCU_SUCCESS;
}
```
