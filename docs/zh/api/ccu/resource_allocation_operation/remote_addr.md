# remote_addr

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

`ccu::remote_addr`是CCU kernel内对端片上内存地址的C++包装类，是“地址（`address`）+ token（`variable`）”的复合对象，结构与[local_addr](local_addr.md)相同。

- 构造即分配：默认构造一次申请`address`和`variable`两个虚拟句柄（分别用于`addr`和`token`）。
- 析构不释放：析构不释放硬件资源，物理资源随CCU实例生命周期统一管理，翻译完成后虚拟句柄失效。

> [!NOTE]说明
> 资源分配采用“先虚后实”两阶段模型：注册阶段的`remote_addr()`构造仅产生虚拟句柄，真正的物理资源在注册阶段统一分配。

`remote_addr`专供跨rank写（`write`/`write_reduce`）使用，指向对端rank的目标内存。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class remote_addr final {
public:
    remote_addr();                       // 默认构造，一次申请1个address和1个variable虚拟句柄
    address addr;                        // 对端片上内存地址字段（地址寄存器）
    variable token;                      // 对端安全token字段（通用寄存器）
    ccu_remote_addr_handle handle{0};    // 复合句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

- 默认构造

    用`remote_addr ra;`申请1个`address`和1个`variable`虚拟句柄。

## 参数说明

| 参数名 | 类型 | 说明 |
| --- | --- | --- |
| `addr` | `address` | 对端片上内存目标地址。须由对端rank的VA（Virtual Address，虚拟地址）与token（对端`asccomm_ccu_get_mem_token`结果）来填充，运算符语义见[address](address.md)。 |
| `token` | `variable` | 对端安全token值。须与`addr`配对，来自对端rank，不可与本端token混用。运算符语义见[variable](variable.md)。 |

## 约束说明

> [!CAUTION]注意
> `token`属于安全信息，host/device日志均不允许打印，禁止以明文形式跨rank透传。`addr`和`token`字段必须是对端rank目标内存的值，与本端`local_addr`字段来源完全不同。

- 只能在注册阶段构造；注册阶段之外构造时，抛出携带`CCU_E_PTR`的异常。
- 默认构造只申请虚拟句柄，在注册阶段内恒成功不抛异常；`address/variable`物理资源不足时，由注册阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- copy/move构造函数只拷贝`handle`、`addr.handle`、`token.handle`三个字段，不申请新的`address/variable`，`remote_addr r2 = r1;`后两个对象指向同一组寄存器；`operator=(const remote_addr&)`则会执行device端寄存器赋值指令（语义与`local_addr`相同），两者不对称。
- 不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- 不可对内嵌的`addr`/`token`字段再单独调用`address()`/`variable()`构造新对象——`remote_addr()`已一次性完成所有子字段的分配。
- `remote_addr`必须与[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)配合使用，通过已建链的channel进行跨rank RDMA操作，脱离channel直接使用时，硬件会发生未定义行为。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 对端rank的地址与token须通过进程间通信（如消息传递框架）从对端获取，
// 再通过kernelArg或taskArgs+load_arg传入kernel。
struct my_kernel_arg {
    ChannelHandle channel_handle;
    uint64_t remote_addr;
    uint64_t remote_token;
    uint64_t local_addr;
    uint64_t local_token;
};

CcuResult my_kernel(ccu_kernel_arg arg) {
    auto* params = static_cast<my_kernel_arg*>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体

    remote_addr remote;
    // 从注册阶段的kernelArg赋值（固化为立即数）
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

    write(ch, remote, local, len, evt);   // 将本端片上内存数据写入对端片上内存
    event_wait(evt);

    return CCU_SUCCESS;
}
```
