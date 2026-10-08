# variable

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

`ccu::variable`是CCU kernel内通用寄存器的C++包装类，用于保存标量值，支持赋值、加法和移位运算。

- 构造即分配：默认构造只申请虚拟句柄。
- 析构不释放：析构不释放硬件资源，物理资源随CCU实例生命周期统一管理，翻译完成后虚拟句柄失效。
- 运算符即device操作：赋值与算术运算符描述的是device端执行的操作，执行阶段操作对应的寄存器，而非host端立即计算。

> [!NOTE]说明
> 资源分配采用“先虚后实”两阶段模型：注册阶段的`variable()`构造仅产生虚拟句柄，真正的物理标量寄存器在注册阶段统一分配。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class variable final {
public:
    // 默认构造：申请1个标量寄存器虚拟句柄
    variable();
    // 预约构造：绑定Host侧已预约的第index个标量寄存器，不申请新的标量寄存器
    explicit variable(ccu_variable_handle var_handle, uint32_t index = 0);

    // 赋值运算符
    void operator=(uint64_t immediate) const;                    // 赋立即数
    void operator=(const variable& other) const;                 // variable间赋值

    // 算术与移位运算符
    void operator+=(const variable& other) const;                // 就地加法
    /*内部表达式对象*/ operator+(const variable& that) const;     // 加法（表达式模板）
    /*内部表达式对象*/ operator<<(const variable& that) const;    // 左移（表达式模板）
    /*内部表达式对象*/ operator>>(const variable& that) const;    // 右移（表达式模板）
    void operator<<=(const variable& other) const;               // 就地左移
    void operator>>=(const variable& other) const;               // 就地右移

    // 条件运算符
    cond_expr operator==(uint64_t immediate);                     // 产出cond_expr
    cond_expr operator!=(uint64_t immediate);                     // 产出cond_expr

    ccu_variable_handle handle{0};                                // 虚拟句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

- 默认构造

    用`variable v;`申请1个标量寄存器虚拟句柄。

- 预约构造

    预约构造`variable v(var_handle, index);`的参数如下：

    | 参数名 | 输入/输出 | 描述 |
    | --- | --- | --- |
    | var_handle | 输入 | 预约句柄，即Host侧调用`asccomm_ccu_variable_alloc`预先从实例资源池划出一段物理标量寄存器后返回的句柄，通过`kernelArg`传入kernel。 |
    | index | 输入 | 要绑定的标量寄存器在预约段内的序号，从0起，默认值为`0`。 |

    多次用同一预约句柄和同一`index`构造，得到的多个`variable`对象共享同一个物理标量寄存器。

## 运算符说明

### 赋值运算符

| 表达式写法 | 硬件语义 |
| --- | --- |
| `v = imm;`（`imm`为`uint64_t`） | `v ← imm`。立即数在注册阶段确定，执行阶段不可变。 |
| `d = s;`（`s`为`variable`） | `d ← s`。device端寄存器赋值，而非host端handle拷贝。 |

### 算术与移位运算符

| 表达式写法 | 硬件语义 |
| --- | --- |
| `r = a + b;` | `r ← a + b`（单条双源加法指令）。`operator+`返回表达式模板对象，被`operator=`消费时生成一条device加法，不产生临时variable。 |
| `r += b;` | `r ← r + b`。与`r = r + b`语义相同，无临时对象。 |
| `r = a << b;` | `r ← a << b`（左移）。`operator<<`返回表达式模板对象，被`operator=`消费时生成一条device移位。 |
| `r = a >> b;` | `r ← a >> b`（右移）。语义同上。 |
| `r <<= b;` | `r ← r << b`。与`r = a << b;`语义相同。 |
| `r >>= b;` | `r ← r >> b`。与`r = a >> b;`语义相同。 |

> [!CAUTION]注意
> `r = a + b`与`r = a << b`使用表达式模板（内部类型）以避免产生临时variable占用额外标量寄存器。不要把`a + b`或`a << b`的结果存入普通C++变量，否则不会生成对应的device操作。

### 条件运算符

| 表达式写法 | 返回类型 | 说明 |
| --- | --- | --- |
| `n == imm` | `cond_expr` | 产出条件表达式对象，不产生任何device操作，专供`CCU_IF`宏使用。 |
| `n != imm` | `cond_expr` | 同上。 |

> [!CAUTION]注意
> `cond_expr`只能被控制流宏使用，不能用作普通C++布尔表达式（如`if (n == 0)`）。塞入普通`if`不会产生任何CCU控制流，表达式被直接丢弃。

## 约束说明

- 只能在注册阶段构造；注册阶段之外构造时，抛出携带`CCU_E_PTR`的异常。
- 默认构造只申请虚拟句柄，在注册阶段内恒成功不抛异常；标量寄存器物理资源不足时，由注册阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- 预约构造会校验预约句柄与`index`，参数不合法时抛出异常，被`<<<>>>`统一接住，对外返回`CCU_E_INTERNAL`，本轮注册被中止。
- copy/move构造函数只拷贝`handle`字段、不申请新的标量寄存器，`variable v2 = v1;`后两个对象是同一个kernel内句柄；如需独立的`variable`，使用默认构造或[`array<variable>`](array.md)。
- 不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- 当前仅支持加法与移位（左移/右移）运算，不支持减法、乘法、除法。
- 立即数不可直接参与算术，须先赋给一个`variable`后再参与运算：`one = 1; v = v + one`。

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    variable n, i, one, step;

    // 赋立即数（注册阶段确定）
    n = 100;
    i = 0;
    one = 1;
    step = 8;

    // variable间赋值
    variable cursor;
    cursor = i;           // cursor ← i

    // 算术：表达式模板写法（r = a + b），仅生成一条device加法
    variable sum;
    sum = i + step;       // sum ← i + step

    // 就地加法
    i += one;             // i ← i + one

    // 条件运算（供CCU_IF宏使用）
    CCU_IF(n == 0) {
        // ...
    }

    return CCU_SUCCESS;
}
```
