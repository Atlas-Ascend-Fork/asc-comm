# remote_addr

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

`ccu::remote_addr`是CCU kernel内对端HBM地址的C++包装类，是"地址（`address`）+ token（`variable`）"的复合对象，结构与[local_addr](local_addr.md)镜像。

- 构造即分配：默认构造函数一次性申请1个GSA（用于`addr`）和1个XN（用于`token`）。
- 析构不释放：析构函数不释放硬件资源；虚拟句柄在翻译完成后失效，物理资源随CCU实例生命周期统一管理、回收。

`remote_addr`专供跨rank写（`write`/`write_reduce`）使用，承载对端rank目标内存的物理地址与安全token。`addr`和`token`必须来自对端rank，不可与本端的`local_addr`字段混用。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class remote_addr final {
public:
    remote_addr();                        // 构造即Alloc（同时申请GSA + XN）
    address addr;                        // 对端HBM地址字段（GSA寄存器）
    variable token;                      // 对端安全token字段（XN寄存器）
    ccu_remote_addr_handle handle{0};      // 复合句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

| 构造形式 | 说明 |
| --- | --- |
| `remote_addr ra;` | 一次性申请1个GSA和1个XN虚拟句柄，回填`ra.handle`/`ra.addr.handle`/`ra.token.handle`三个句柄。 |

C++构造只申请虚拟句柄：在kernel注册阶段内调用时恒成功；若不在kernel注册阶段调用，则构造时抛出异常（携带错误码`CCU_E_PTR`）。GSA/XN物理资源不足时，在`HcommCcuKernelRegister`阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。

> [!CAUTION]注意
> `remote_addr`类有copy/move构造函数，它们只拷贝`handle`、`addr.handle`、`token.handle`三个字段，不申请新的GSA/XN，`remote_addr r2 = r1;`后`r1`和`r2`指向同一组寄存器。`operator=(const remote_addr&)`则会执行device端寄存器赋值指令（语义与`local_addr`相同），两者不对称，使用时需特别留意。

## 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `addr` | `address` | 对端HBM目标地址。须由对端rank的VA与token（对端`HcommCcuGetMemToken`结果）来填充，运算符语义见[address](address.md)。 |
| `token` | `variable` | 对端安全token值。须与`addr`配对，来自对端rank，不可与本端token混用。运算符语义见[variable](variable.md)。 |

## 约束说明

> [!CAUTION]注意
> `token`属于安全信息，host/device日志均不允许打印，禁止以明文形式跨rank透传。`addr`和`token`字段必须是对端rank目标内存的值，与本端`local_addr`字段来源完全不同。

- 只能在kernel注册阶段构造remote_addr。
- 析构不释放硬件资源，不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- 不可对内嵌的`addr`/`token`字段再单独调用`address()`/`variable()`构造新对象——`remote_addr()`已一次性完成所有子字段的分配。
- `remote_addr`必须与`ChannelHandle`配合使用，通过已建链的channel进行跨rank RDMA操作，脱离channel直接使用时，硬件会发生未定义行为。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 对端rank的地址与token须通过进程间通信（如消息传递框架）从对端获取，
// 再通过kernelArgs或task_args+load_arg传入kernel。

CcuResult my_kernel(ccu_kernel_arg arg) {
    auto* params = static_cast<my_kernel_arg*>(arg);  // ccu_kernel_arg 为void*，先转型为用户入参结构体

    remote_addr remote;
    // 从注册阶段的kernelArgs赋值（固化为立即数）
    remote.addr = params->remote_addr;
    remote.token = params->remote_token;

    // 配合跨rank write/write_reduce接口使用
    ChannelHandle ch = params->channel_handle;
    local_addr local;
    local.addr = params->local_addr;
    local.token = params->local_token;
    variable len;
    event evt;
    len = 1024;

    write(ch, remote, local, len, evt);   // 将本端HBM数据写入对端HBM
    event_wait(evt);

    return CCU_SUCCESS;
}
```
