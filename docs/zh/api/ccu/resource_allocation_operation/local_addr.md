# local_addr

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

`ccu::local_addr`是CCU kernel内本端片上内存地址的C++包装类，是“地址（`address`）+ token（`variable`）”的复合对象。

- 构造即分配：默认构造一次申请`address`和`variable`两个虚拟句柄，分别用于`addr`和`token`。
- 析构不释放：析构不释放硬件资源，物理资源随CCU实例生命周期统一管理，翻译完成后虚拟句柄失效。

> [!NOTE]说明
> 资源分配采用“先虚后实”两阶段模型：注册阶段的`local_addr()`构造仅产生虚拟句柄，真正的物理资源在注册阶段统一分配。

CCU硬件不接受进程虚拟地址，访问片上内存必须使用由`asccomm_ccu_get_mem_token`换算后的token。`local_addr`的`addr`字段存储物理地址或token化后的VA（Virtual Address，虚拟地址），`token`字段存储配套的安全token值。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class local_addr final {
public:
    local_addr();                         // 默认构造，一次申请address和variable两个虚拟句柄
    address addr;                        // 本端片上内存地址字段（地址寄存器）
    variable token;                      // 安全token字段（通用寄存器）
    ccu_local_addr_handle handle{0};       // 复合句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

- 默认构造

    用`local_addr la;`申请`address`和`variable`两个虚拟句柄。

## 参数说明

`local_addr`类的参数说明如下：

| 参数名 | 类型 | 说明 |
| --- | --- | --- |
| `addr` | `address` | 本端片上内存地址。通过`la.addr = imm`（立即数）或`la.addr = var`（variable）赋值，运算符语义见[address](address.md)。 |
| `token` | `variable` | 安全token值。由host端调用`asccomm_ccu_get_mem_token`获取后，通过`kernelArg`或`taskArgs`+`load_arg`传入kernel，再赋值给`la.token`。运算符语义见[variable](variable.md)。 |

## 约束说明

> [!CAUTION]注意
> `token`属于安全信息，host/device日志均不允许打印，禁止以明文形式跨rank透传。

- 只能在注册阶段构造；注册阶段之外构造时，抛出携带`CCU_E_PTR`的异常。
- 默认构造只申请虚拟句柄，在注册阶段内恒成功不抛异常；`address/variable`物理资源不足时，由注册阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- copy/move构造函数只拷贝`handle`、`addr.handle`、`token.handle`三个字段，不申请新的`address/variable`，`local_addr l2 = l1;`后两个对象指向同一组寄存器；`operator=(const local_addr&)`则会发出两条device端寄存器赋值指令（把`other.addr`、`other.token`的值搬移过来），两者语义不对称。
- 不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- 不可对内嵌的`addr`/`token`字段再单独调用`address()`/`variable()`构造新对象——`local_addr()`已一次性完成所有子字段的分配。
- `addr`字段须赋值为CCU可访问的物理地址或经token化的VA，直接传入未经token化的进程虚拟地址将触发驱动错误。
- `token`通常来自host端`asccomm_ccu_get_mem_token`调用，须与`addr`配对，不可与对端token混用。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 用户自定义的kernel入参结构体，Host侧填充后通过kernelArg传入
// （src_addr为本端片上内存地址，src_token由Host调用asccomm_ccu_get_mem_token换算获得）
struct my_kernel_arg {
    uint64_t src_addr;   // 本端片上内存地址
    uint64_t src_token;  // 配套的安全token
};

CcuResult my_kernel(ccu_kernel_arg arg) {  // arg为void*，指向Host传入的my_kernel_arg
    auto* params = static_cast<my_kernel_arg*>(arg);

    local_addr src;
    // 方式1：从注册阶段的kernelArg赋值（固化为立即数）
    src.addr = params->src_addr;
    src.token = params->src_token;

    // 方式2：通过taskArgs在执行阶段注入（更灵活）
    variable addrVar, tokenVar;
    load_arg(addrVar, 0);    // taskArgs[0]为地址
    load_arg(tokenVar, 1);   // taskArgs[1]为token
    local_addr src2;
    src2.addr = addrVar;     // 执行阶段动态地址
    src2.token = tokenVar;

    return CCU_SUCCESS;
}
```
