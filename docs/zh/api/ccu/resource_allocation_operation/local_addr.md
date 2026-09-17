# local_addr

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="910" id4 -->
- Atlas 训练系列产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品：不支持
<!-- end id5 -->

## 功能说明

头文件为：`#include "ccu/ccu_primitives.hpp"`

`ccu::local_addr`是CCU kernel内本端HBM地址的C++包装类，是"地址（`address`）+ token（`variable`）"的复合对象。

- 构造即分配：默认构造函数一次性申请1个GSA（用于`addr`）和1个XN（用于`token`）。
- 析构不释放：析构函数不释放硬件资源；虚拟句柄在翻译完成后失效，物理资源随CCU实例生命周期统一管理、回收。

CCU硬件不接受进程虚拟地址，访问HBM必须使用由`HcommCcuGetMemToken`（host端调用）换算后的token。`local_addr`的`addr`字段存储物理地址（或token化后的VA），`token`字段存储配套的安全token值。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class local_addr final {
public:
    local_addr();                         // 构造即Alloc（同时申请GSA + XN）
    address addr;                        // 本端HBM地址字段（GSA寄存器）
    variable token;                      // 安全token字段（XN寄存器）
    ccu_local_addr_handle handle{0};       // 复合句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

| 构造形式 | 说明 |
| --- | --- |
| `local_addr la;` | 一次性申请1个GSA和1个XN虚拟句柄，回填`la.handle`/`la.addr.handle`/`la.token.handle`三个句柄。 |

C++构造只申请虚拟句柄：在kernel注册阶段内调用时恒成功；若不在kernel注册阶段调用，则构造时抛出异常（携带错误码`CCU_E_PTR`）。GSA/XN物理资源不足时，在`HcommCcuKernelRegister`阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。

> [!CAUTION]注意
> `local_addr`类有copy/move构造函数，它们只拷贝`handle`、`addr.handle`、`token.handle`三个字段，不申请新的GSA/XN，`local_addr l2 = l1;`后`l1`和`l2`指向同一组寄存器。但`operator=(const local_addr& other)`（赋值，不是构造）不是handle拷贝：它会执行`this->addr = other.addr; this->token = other.token;`，即发出两条device端寄存器赋值指令（参见`address::operator=`、`variable::operator=`），操作的是两组独立寄存器之间的值搬移。两者语义不对称，使用时需特别留意。

## 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `addr` | `address` | 本端HBM地址。通过`la.addr = imm`（立即数）或`la.addr = var`（variable）赋值，运算符语义见[address](address.md)。 |
| `token` | `variable` | 安全token值。由host端调用`HcommCcuGetMemToken`获取后，通过`kernelArgs`或`task_args`+`load_arg`传入kernel，再赋值给`la.token`。运算符语义见[variable](variable.md)。 |

## 约束说明

> [!CAUTION]注意
> `token`属于安全信息，host/device日志均不允许打印，禁止以明文形式跨rank透传。

- 只能在kernel注册阶段构造local_addr。
- 析构不释放硬件资源，不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- 不可对内嵌的`addr`/`token`字段再单独调用`address()`/`variable()`构造新对象——`local_addr()`已一次性完成所有子字段的分配。
- `addr`字段须赋值为CCU可访问的物理地址或经token化的VA，直接传入未经token化的进程虚拟地址将触发驱动错误。
- `token`通常来自host端`HcommCcuGetMemToken`调用，须与`addr`配对，不可与对端token混用。

## 调用示例

```cpp
using namespace AscendC::ccu;

// host端（kernel注册前）：
// uint64_t tokenInfo = 0;
// HcommCcuGetMemToken(srcVa, size, &tokenInfo);
// kernel_arg.src_addr = src_va;
// kernel_arg.src_token = token_info;

CcuResult my_kernel(ccu_kernel_arg arg) {
    auto* params = static_cast<my_kernel_arg*>(arg);  // ccu_kernel_arg 为void*，先转型为用户入参结构体

    local_addr src;
    // 从注册阶段的kernelArgs赋值（固化为立即数）
    src.addr = params->src_addr;
    src.token = params->src_token;

    // 或者通过task_args在执行阶段注入（更灵活）
    variable addrVar, tokenVar;
    load_arg(addrVar, 0);    // task_args[0]为地址
    load_arg(tokenVar, 1);   // task_args[1]为token
    local_addr src2;
    src2.addr = addrVar;     // 执行阶段动态地址
    src2.token = tokenVar;

    return CCU_SUCCESS;
}
```
