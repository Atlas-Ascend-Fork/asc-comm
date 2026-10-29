# CCU_WHILE

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

`CCU_WHILE`是一个预处理器宏，承担两种语义，由其前面是否有`CCU_DO`自动区分：

1. 独立while循环：`CCU_WHILE(cond_expr) { body }`——当`CCU_WHILE`前没有`CCU_DO`时，作为普通while循环：每轮先判断条件，条件满足才执行body。
2. do-while收尾：`CCU_DO { body } CCU_WHILE(cond_expr);`——当`CCU_WHILE`前有`CCU_DO`时，作为do-while循环的收尾：执行完body后判断条件，条件满足则回到循环开头。

两种语义对用户完全透明，写法与C++标准while/do-while一致。do-while用法请参见[CCU_DO](CCU_DO.md)。

## 函数原型

```cpp
// 语义1：独立while 循环
CCU_WHILE(cond_expr) {
    // body
}

// 语义2：do-while 收尾（必须紧跟CCU_DO { } 之后）
CCU_DO {
    // body
} CCU_WHILE(cond_expr);
```

## 参数说明

| 参数名 | 描述 |
| --- | --- |
| cond_expr | 条件表达式，类型为`AscendC::ccu::cond_expr`。通过`ccu::variable`的`operator==(uint64_t)`或`operator!=(uint64_t)`产生，例如`counter == 10`。条件在执行阶段由CCU硬件计算。 |

`cond_expr`的定义与生成方式请参见[CCU_IF](CCU_IF.md#参数说明)。

## 返回值说明

`CCU_WHILE`为预处理器宏，本身不返回[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)。宏展开后若发生内部错误（如label重复、句柄无效），可能产生`CCU_E_PARA`/`CCU_E_NOT_FOUND`，此时宏不抛异常、也不阻断kernel生成：失败时body会被跳过，但kernel生成仍然成功，错误不通过返回值反馈，调试时需留意。

## 约束说明

- `CCU_WHILE(cond_expr)`后必须紧跟{}包裹的循环代码块。
- `cond_expr`仅支持`==`和`!=`两种比较运算，`<`、`>`、`<=`、`>=`在所有产品上均不支持。
- `cond_expr`中比较的一方须为`uint64_t`立即数。针对Ascend 950PR&950DT系列产品，不支持`variable`与`variable`之间的比较。
- 作为独立while循环使用时，`CCU_WHILE`可以嵌套，内层`CCU_WHILE`可在外层`CCU_WHILE`的body内使用。
- `CCU_WHILE`（独立while）可以与`CCU_IF`组合嵌套。
- `CCU_WHILE`（无论哪种语义）不可在`ccu::loop`的body lambda内部使用，硬件loop body内不支持软件分支/循环。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 场景：独立while循环，对计数器循环10次
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable counter, one;
    counter = 0;
    one = 1;

    CCU_WHILE(counter != 10ULL) {
        // 循环体
        counter = counter + one;
    }

    return CCU_SUCCESS;
}
```
