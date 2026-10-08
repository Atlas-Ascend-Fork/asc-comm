# event

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

`ccu::event`是CCU kernel内同步寄存器槽位的C++包装类，用于标记异步操作（如数据搬运）的完成状态。

- 构造即分配：默认构造只申请虚拟句柄。
- 析构不释放：析构不释放硬件资源，物理资源随CCU实例生命周期统一管理，翻译完成后虚拟句柄失效。

> [!NOTE]说明
> 资源分配采用“先虚后实”两阶段模型：注册阶段的`event()`构造仅产生虚拟句柄，真正的物理event槽位在注册阶段统一分配。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class event final {
public:
    // 默认构造：申请1个event槽位虚拟句柄
    event();

    // 预约构造：绑定Host侧已预约的第index个event槽位，不申请新的event槽位
    explicit event(ccu_event_handle acq_handle, uint32_t index = 0);

    ccu_event_handle handle{0};  // 虚拟句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

- 默认构造

    用`event e;`申请1个完成事件槽位虚拟句柄。

- 预约构造

    预约构造的参数如下：

    | 参数名 | 输入/输出 | 描述 |
    | --- | --- | --- |
    | acq_handle | 输入 | 预约句柄，即Host侧调用`asccomm_ccu_event_alloc`预先从实例资源池划出一段物理event槽位后返回的句柄，通过`kernelArg`传入kernel。 |
    | index | 输入 | 要绑定的event槽位在预约段内的序号，从0起，默认值为`0`。 |

    多次用同一预约句柄和同一`index`构造，得到的多个`event`对象共享同一个物理event槽位。

## 约束说明

- 只能在注册阶段构造；注册阶段之外构造时，抛出携带`CCU_E_PTR`的异常。
- 默认构造只申请虚拟句柄，在注册阶段内恒成功不抛异常；event槽位物理资源不足时，由注册阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- 预约构造会校验预约句柄与`index`，参数不合法时抛出异常，被`<<<>>>`统一接住，对外返回`CCU_E_INTERNAL`，本轮注册被中止。
- copy/move构造函数只拷贝`handle`字段、不申请新的event槽位，`event e2 = e1;`后两个对象是同一个kernel内句柄，对任一对象`event_record`/`event_wait`操作的是同一份硬件状态位；如需独立的`event`，使用默认构造或[`array<event>`](array.md)。
- 不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- `event`没有`mask`字段，`mask`在`event_record`/`event_wait`及数据搬运接口中以参数形式传入，C++默认值为`1`。同一个`event`的不同bit相互独立，可承载多组配对。

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    event evt;    // 申请1个event槽位

    // 配合数据搬运接口使用：硬件搬运完成时自动将evt中mask对应的标志位设置为1
    local_addr src, dst;
    variable len;
    local_copy(dst, src, len, evt);    // mask默认0x1
    event_wait(evt);                    // 等待bit0被设置为1

    return CCU_SUCCESS;
}
```
