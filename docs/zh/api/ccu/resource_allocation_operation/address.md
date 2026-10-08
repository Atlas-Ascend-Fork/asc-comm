# address

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

`ccu::address`是CCU kernel内地址寄存器的C++包装类，用于保存地址值，支持赋值和加法运算。

- 构造即分配：默认构造只申请虚拟句柄。
- 析构不释放：析构不释放硬件资源，物理资源随CCU实例生命周期统一管理，翻译完成后虚拟句柄失效。
- 运算符即device操作：赋值与算术运算符描述的是device端执行的操作，执行阶段操作对应的地址寄存器，而非host端立即计算。

> [!NOTE]说明
> 资源分配采用“先虚后实”两阶段模型：注册阶段的`address()`构造仅产生虚拟句柄，真正的物理地址寄存器在注册阶段统一分配。

与[`variable`](variable.md)保存标量值不同，`address`专门保存地址值，即片上内存物理地址，承载的是device端地址值，不能在host端直接解引用；典型用法是将基地址与偏移量相加得到目标地址，供数据搬运接口使用。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class address final {
public:
    // 默认构造，申请1个address虚拟句柄
    address();

    // 赋值运算符
    void operator=(uint64_t immediate) const;                   // 赋立即数地址
    void operator=(const variable& var) const;                  // variable值赋给address
    void operator=(const address& other) const;                 // address间赋值

    // 算术运算符
    void operator+=(const variable& var) const;                 // address就地加variable
    void operator+=(const address& other) const;                // address就地加address
    /*内部表达式对象*/ operator+(const address& that) const;    // address+address
    /*内部表达式对象*/ operator+(const variable& var) const;    // address+variable

    ccu_address_handle handle{0};                                 // 虚拟句柄
};
// variable + address（交换律，全局函数）
/*内部表达式对象*/ operator+(const variable& var, const address& addr);
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

- 默认构造

    用`address a;`申请1个`address`虚拟句柄。

## 运算符说明

### 赋值运算符

| 表达式写法 | 硬件语义 |
| --- | --- |
| `addr = imm;`（`imm`为`uint64_t`） | `addr ← imm`。地址立即数在注册阶段确定，执行阶段不可变。 |
| `addr = var;`（`var`为`variable`） | `addr ← var`。将variable在执行阶段的值写入地址寄存器，适用于执行阶段动态地址。 |
| `dst = src;`（`src`为`address`） | `dst ← src`。address间寄存器赋值。 |

### 算术运算符

| 表达式写法 | 硬件语义 |
| --- | --- |
| `r = a + b;`（`a/b`均为`address`） | `r ← a + b`。 |
| `r = addr + var;` / `r = var + addr;` | `r ← addr + var`。两种写法语义相同（交换律）。 |
| `addr += var;`（`var`为`variable`） | `addr ← addr + var`（就地操作，比`addr = addr + var`节省一条指令）。 |
| `addr += other;`（`other`为`address`） | `addr ← addr + other`。 |

## 约束说明

- 只能在注册阶段构造；注册阶段之外构造时，抛出携带`CCU_E_PTR`的异常。
- 默认构造只申请虚拟句柄，在注册阶段内恒成功不抛异常；`address`物理资源不足时，由注册阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- copy/move构造函数只拷贝`handle`字段、不申请新的`address`，`address a2 = a1;`后两个对象指向同一个地址寄存器；`operator=(const address&)`则会发出device端寄存器赋值指令。如需独立的`address`，必须显式默认构造。
- 不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- `address`不可被赋值到`variable`，即不提供`variable = address`操作。
- 当前仅支持加法运算，不支持减法、乘法、除法。
- 立即数不可直接参与算术，`addr + 0x100`不合法，须先将偏移量赋给一个`variable`后再参与运算。

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    address base, dst;
    variable offset, stride;

    // 赋立即数地址（注册阶段固化）
    base = 0x80000000ULL;

    // 赋variable值到address（执行阶段动态地址）
    // offset通过load_arg在下发时注入
    load_arg(offset, 0);
    dst = offset;         // dst ← offset（执行阶段确定）

    // address + variable偏移（两种等价写法）
    stride = 4096;
    dst = base + stride;  // r = addr + var
    dst = stride + base;  // r = var + addr（等价）

    // address就地偏移
    base += stride;        // base += 4096

    // address + address
    address result;
    result = base + dst;

    return CCU_SUCCESS;
}
```
