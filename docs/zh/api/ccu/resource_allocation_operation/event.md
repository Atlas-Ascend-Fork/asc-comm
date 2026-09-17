# event

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

`ccu::event`是CCU kernel内event单元的C++包装类。

- 构造即分配：默认构造函数自动申请1个event单元虚拟句柄。
- 析构不释放：析构函数不释放硬件资源；虚拟句柄在翻译完成后失效，物理资源随CCU实例生命周期统一管理、回收。

`ccu::event`用于标记异步操作（如数据搬运）的完成状态。数据搬运接口的末位参数`(event, mask)`中，硬件在搬运完成时自动将`event`中`mask`对应的标志位设置为1，下游通过`event_wait(event, mask)`等待。`event`本身不持有`mask`字段，`mask`在每次`event_record`/`event_wait`调用时独立传入。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
class event final {
public:
    event();                    // 构造即Alloc
    // 绑定Host侧已预约的第index个event单元，不申请新的event单元
    explicit event(ccu_event_handle acq_handle, uint32_t index = 0);
    ccu_event_handle handle{0};  // 虚拟句柄
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

| 构造形式 | 说明 |
| --- | --- |
| `event e;` | 申请1个event单元虚拟句柄。只能在kernel注册阶段（`HcommCcuKernelRegister`执行的kernel函数体内）调用。 |
| `event e(acq_handle, index);` | 绑定Host侧通过[HcommCcuEventAlloc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_resource_mgmt/HcommCcuEventAlloc.md)预约的第`index`个event单元，不申请新的event单元。`index`默认值为`0`。 |

### 绑定Host侧预约资源的构造

`explicit event(ccu_event_handle acq_handle, uint32_t index = 0)`中的`acq_handle`是Host侧[HcommCcuEventAlloc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_resource_mgmt/HcommCcuEventAlloc.md)返回的**预约句柄**，不是另一个`event`的`handle`。不能传入[variable](variable.md)的预约句柄。典型用法是把预约句柄通过`kernelArgs`传入kernel，再在kernel内绑定其中某个event单元。

绑定后的Event按普通Event使用`event_record`/`event_wait`。对同一预约句柄的同一`index`多次调用本构造函数，每次得到新的kernel内`handle`，指向同一个物理event单元。这与copy/move不是一条路径：`event e2 = e1;`只拷贝已有`handle`，不走本构造。

构造失败时抛出异常，会被[HcommCcuKernelRegister](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_kernel_launch_execution/HcommCcuKernelRegister.md)统一接住，对外返回`CCU_E_INTERNAL`，本轮注册被中止。

> [!CAUTION]注意
> copy/move构造函数只拷贝`handle`字段、不申请新的event单元。`event e2 = e1;`后两个对象是同一个kernel内句柄，对任一对象`event_record`/`event_wait`操作的是同一份硬件状态位。如需独立的`event`，使用默认构造、绑定其他`index`，或用[`array<event>`](array.md)。

## 返回值

构造成功后，`e.handle`字段持有分配到的虚拟句柄。句柄值由框架在注册阶段分配，首个`event`的handle为`0`、后续递增，不要用"`handle != 0`"判断有效性。

## 约束说明

- 只能在kernel注册阶段构造Event。
- 析构不释放硬件资源，不应在kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- `event`没有`mask`字段，`mask`在`event_record`/`event_wait`及数据搬运接口中以参数形式传入，C++默认值为`1`。同一个`event`的不同bit相互独立，可承载多组配对。
- 默认构造只申请虚拟句柄，恒成功不抛异常；event单元物理资源不足时，由`HcommCcuKernelRegister`阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。如需多个`event`，可使用[`array<event>`](array.md)批量申请。
- 预约句柄构造`event e(acq_handle, index);`同样只能在kernel注册阶段调用，会校验预约句柄与`index`，参数不合法时抛出异常，不再保证恒成功。

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    event evt;    // 申请1个event单元

    // 配合数据搬运接口使用：硬件搬运完成时自动将evt中mask对应的标志位设置为1
    local_addr src, dst;
    variable len;
    local_copy(dst, src, len, evt);    // mask默认0x1
    event_wait(evt);                    // 等待bit0被设置为1

    return CCU_SUCCESS;
}
```
