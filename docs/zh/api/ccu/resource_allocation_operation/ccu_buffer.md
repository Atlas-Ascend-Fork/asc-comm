# ccu_buffer

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

`ccu::ccu_buffer`是CCU kernel内片上CCU Buffer的C++包装类，每个切片固定4096个字节，用于在片上内存与对端之间中转数据。

- 构造即分配：默认构造只申请虚拟句柄。
- 析构不释放：析构不释放硬件资源，物理资源随CCU实例生命周期统一管理，翻译完成后虚拟句柄失效。

> [!NOTE]说明
> 资源分配采用“先虚后实”两阶段模型：注册阶段的`ccu_buffer()`构造仅产生虚拟句柄，真正的物理CCU Buffer切片在注册阶段统一分配。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class ccu_buffer final {
public:
    ccu_buffer();                      // 默认构造，申请1块CCU Buffer切片虚拟句柄
    ccu_buffer_handle handle{0};       // 虚拟句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

- 默认构造

    用`ccu_buffer buf;`申请1块CCU Buffer切片虚拟句柄。

## 约束说明

- 只能在注册阶段构造；注册阶段之外构造时，抛出携带`CCU_E_PTR`的异常。
- 默认构造只申请虚拟句柄，在注册阶段内恒成功不抛异常；CCU Buffer物理资源不足时，由注册阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- copy/move构造函数只拷贝`handle`字段、不申请新的CCU Buffer切片，`ccu_buffer b2 = b1;`后两个对象指向同一块切片；如需独立的`ccu_buffer`，必须显式默认构造或用[`array<ccu_buffer>`](array.md)。
- 不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- 每个`ccu_buffer`固定代表4096字节的片上切片，大小不可配置。
- 操作`ccu_buffer`的接口（如`local_copy`、`write`）传入的`len`不可超过4096字节。该上限须由调用方自行保证，超出时硬件会发生未定义行为（数据截断/越界等）。

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    ccu_buffer buf;    // 申请1块4KB CCU Buffer切片
    local_addr src;
    variable len;
    event evt;

    // 将片上内存数据拷贝到CCU Buffer
    local_copy(buf, src, len, evt);
    event_wait(evt);

    return CCU_SUCCESS;
}
```
