# func

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

`ccu::func`将一个lambda封装为可复用的函数块（FuncBlock）：构造时仅记录lambda，不生成指令；首次通过[call_func](call_func.md)调用时执行lambda，把逻辑合成为一份指令块；后续每次调用只追加一条跳转指令，复用同一份指令块，从而节省指令空间。lambda形参为零个或多个`ccu::variable`，在调用时绑定实参，返回`void`。

> [!NOTE]说明
> 本接口仅完成函数块的定义，调用方式参见[call_func](call_func.md)。

## 类声明

```cpp
namespace AscendC {
namespace ccu {

class func {
public:
    // Lambda必须返回void，形参必须全部为ccu::variable
    template <typename Lambda>
    explicit func(Lambda body);

    func(const func &) = delete;             // 不可复制
    func &operator=(const func &) = delete;  // 不可赋值
};

} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| body | 输入 | lambda表达式，形参为零个或多个`ccu::variable`，返回`void`。形参个数在编译期由lambda签名自动推导。 |

## 返回值说明

无返回值。

## 约束说明

- `ccu::func`不可复制，拷贝构造和赋值运算符已`= delete`，每个`ccu::func`对象有唯一身份。
- lambda形参类型须能从`ccu::variable&`传入，可写为`ccu::variable`、`ccu::variable&`或`const ccu::variable&`，其他类型如`int`、`ccu::address`会编译报错。
- lambda必须返回`void`，否则编译期报错。
- 不同的`ccu::func`对象即使lambda内容相同，也各自合成独立的FuncBlock。

## 调用示例

```cpp
using namespace AscendC::ccu;

static func add_dbl([](variable x) {
    variable tmp;
    tmp = x + x;   // 对x翻倍
});
```
