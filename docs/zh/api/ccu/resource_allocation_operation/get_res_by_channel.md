# get_res_by_channel

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

在CCU kernel内获取指向channel共享`variable`槽位的句柄。

该槽位是channel在建链时就已在本端预留好的、与对端按编号一一配对的共享`variable`，本接口不消耗新的通用寄存器，只是将已经存在的槽位包装成可操作的`variable`对象。

典型场景：对端rank通过[write_variable_with_notify](../synchronization/write_variable_with_notify.md)将值写入本端channel的共享`variable`槽位N，本端kernel用`get_res_by_channel<variable>(ch, N)`取到指向同一槽位的句柄，即可读到对端发来的值。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
template <typename T>
T get_res_by_channel(ChannelHandle channel, uint32_t index);

// 当前仅支持variable特化：
template <>
variable get_res_by_channel<variable>(ChannelHandle channel, uint32_t var_index);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

模板参数`T`为获取的资源类型，当前仅支持`variable`。

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| channel | 输入 | 跨rank通道句柄，类型为[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)，须已完成建链。 |
| var_index | 输入 | 通道内变量索引（0起），对应channel建链时预分配的一组variable槽位中的一个。 |

## 返回值说明

返回一个`variable`对象，指向channel的第`var_index`个共享`variable`槽位。其释放权归channel所有，不随返回的`variable`析构。

调用失败时抛出异常（携带错误码），常见错误码：

| 情形 | 错误码 |
| --- | --- |
| `channel == nullptr`、channel类型非法、接口在注册阶段之外被调用 | `CCU_E_PTR` |
| `var_index`越界（超过channel预分配variable池大小） | `CCU_E_PARA` |

## 约束说明

- 只能在注册阶段调用。
- 与`variable v;`（普通构造）的分配行为完全不同，不可互换。
- 返回的`variable`生命周期归channel管理，在channel销毁前有效。
- 当前仅支持`T = variable`，对其他类型调用编译期失败。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 用户自定义的kernel入参结构体，Host侧填充后通过kernelArg传入
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// 端到端场景：对端通过write_variable_with_notify向本端channel的共享variable槽位0写值，
// 本端kernel通过get_res_by_channel读取该值

CcuResult my_kernel(ccu_kernel_arg arg) {  // arg为void*，指向Host传入的my_kernel_arg
    auto* params = static_cast<my_kernel_arg*>(arg);
    ChannelHandle ch = params->channel_handle;

    // 获取channel预分配的variable[0]，不申请新variable
    variable sync_var = get_res_by_channel<variable>(ch, 0);

    // 等待对端写入
    notify_wait(ch, /*local_notify_idx=*/0);

    // 此时sync_var已持有对端写入的值，可读取参与后续运算
    variable result;
    result = sync_var;

    return CCU_SUCCESS;
}
```
