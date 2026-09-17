# get_res_by_channel

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

在CCU kernel内获取指向channel共享`variable`单元的句柄。

该单元是channel在建链时就已在本端预留好的、与对端编号一一镜像的共享`variable`，本接口不消耗新的XN寄存器，只是将已经存在的单元包装成可操作的`variable`对象。

典型场景：对端rank通过[write_variable_with_notify](../synchronization/write_variable_with_notify.md)将值写入本端channel的共享`variable`单元N，本端kernel用`get_res_by_channel<variable>(ch, N)`取到指向同一单元的句柄，即可读到对端发来的值。

> [!NOTE]说明
> 当前仅支持`variable`特化（`get_res_by_channel<variable>`）。对其他类型的模板实例化会在编译期报错。

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

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| T | 获取的资源类型，当前仅支持`variable`。 |

**表2** 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| channel | 输入 | 跨rank通道句柄（`ChannelHandle`）。channel须已完成建链，其预分配的variable单元须包含`var_index`对应的一个。 |
| var_index | 输入 | 通道内变量索引（0起），对应channel在建链时预分配的一组variable单元中的一个。 |

## 返回值说明

返回一个`variable`对象，其`handle`指向channel的第`var_index`个共享`variable`单元。该`variable`不消耗新的XN，其释放权归channel所有，不随返回的`variable`析构。

调用失败时抛出异常（携带错误码），常见错误码：

| 情形 | 错误码 |
| --- | --- |
| `channel == nullptr`、channel 类型非法、当前不存在处于注册中的kernel | `CCU_E_PTR` |
| `var_index` 越界（超过channel预分配variable池大小） | `CCU_E_PARA` |

## 约束说明

- 只能在kernel注册阶段调用，须在`HcommCcuKernelRegister`执行的kernel函数体内。
- 不消耗新的XN资源，与`variable v;`（普通构造）的分配行为完全不同，不可互换。
- 返回的`variable`生命周期归channel管理，在channel销毁前有效。
- 当前仅支持`T = variable`，对其他类型调用编译期失败。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 端到端场景：对端通过write_variable_with_notify向本端channel的共享variable单元0写值，
// 本端kernel通过get_res_by_channel读取该值

CcuResult my_kernel(ccu_kernel_arg arg) {
    auto* params = static_cast<my_kernel_arg*>(arg);
    ChannelHandle ch = params->channel_handle;

    // 获取channel预分配的variable[0]，不申请新Xn
    variable sync_var = get_res_by_channel<variable>(ch, 0);

    // 等待对端写入
    notify_wait(ch, /*local_notify_idx=*/0);

    // 此时sync_var已持有对端写入的值，可读取参与后续运算
    variable result;
    result = sync_var;

    return CCU_SUCCESS;
}
```
