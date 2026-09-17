# array

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

`ccu::array<T>`是CCU kernel内批量持有物理连续资源的C++模板类。

- `array(count)`构造即批量分配：一次性申请`count`个物理连续的虚拟句柄。
- `array(acq_handle, count)`绑定Host侧已预约的前`count`个资源，不申请新的物理资源。
- 析构不释放：析构函数不释放硬件资源；虚拟句柄在翻译完成后失效，物理资源随CCU实例生命周期统一管理、回收。
- 仅可移动：禁止拷贝，允许移动。

当前仅支持以下三种特化类型：

| 特化类型 | 资源描述 | 支持的构造形式 |
| --- | --- | --- |
| `array<variable>` | N个物理连续标量寄存器 | `array(count)`、`array(acq_handle, count)` |
| `array<event>` | N个物理连续event单元 | `array(count)`、`array(acq_handle, count)` |
| `array<ccu_buffer>` | `n`个物理连续ccu_buffer | 仅`array(count)` |

对其他类型实例化`array<T>`将在编译期报错（仅支持上述三种特化类型）。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
template <typename T>  // T 仅支持variable / event / ccu_buffer
class array final {
public:
    explicit array(uint32_t count);       // 构造即批量虚拟分配，count可为0
    // 绑定Host侧已预约的前count个资源，仅variable / event特化可用
    array(typename ccu_array_traits<T>::Handle acq_handle, uint32_t count);
    T& operator[](uint32_t i);            // 下标访问（无边界检查）
    const T& operator[](uint32_t i) const;
    T* data();                             // 获取首元素指针（用于传给需要指针参数的批量接口）
    const T* data() const;
    uint32_t size() const;                 // 返回元素个数
    // 禁止拷贝；允许移动
    array(const array&) = delete;
    array& operator=(const array&) = delete;
    array(array&& other) noexcept;
    array& operator=(array&& other) noexcept;
};
} // namespace ccu
} // namespace AscendC
```

## 构造函数说明

| 构造形式 | 说明 |
| --- | --- |
| `array<variable> vars(n);` | 申请`n`个物理连续标量寄存器句柄。 |
| `array<event> evts(n);` | 申请`n`个物理连续event单元句柄。 |
| `array<ccu_buffer> bufs(n);` | 申请`n`个物理连续CCU Buffer句柄。 |
| `array<variable> vars(acq_handle, n);` | 绑定Host侧预约的前`n`个标量寄存器，不申请新的标量寄存器。 |
| `array<event> evts(acq_handle, n);` | 绑定Host侧预约的前`n`个event单元，不申请新的event单元。 |

`count`可以为0，两种构造形式均可（见下方注意）。构造失败时抛出异常。

### 绑定Host侧预约资源的构造

`array(acq_handle, count)`中的`acq_handle`须与`T`对应：`array<variable>`用[HcommCcuVariableAlloc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_resource_mgmt/HcommCcuVariableAlloc.md)返回的预约句柄，`array<event>`用[HcommCcuEventAlloc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_resource_mgmt/HcommCcuEventAlloc.md)返回的预约句柄。构造时依次绑定预约段内序号`0`到`count - 1`的资源，元素`arr[i]`对应预约段内的第`i`个资源，与[HcommCcuVariableGetAddr](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_resource_mgmt/HcommCcuVariableGetAddr.md)、[HcommCcuEventGetAddr](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_resource_mgmt/HcommCcuEventGetAddr.md)中的`index`一一对应。

该构造只对`array<variable>`和`array<event>`可用。`array<ccu_buffer>`没有对应的实现，写成`array<ccu_buffer> bufs(acq_handle, n);`会在编译期失败。

构造失败时抛出异常，会被[HcommCcuKernelRegister](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/ccu_kernel_launch_execution/HcommCcuKernelRegister.md)统一接住，对外返回`CCU_E_INTERNAL`，本轮注册被中止。

> [!CAUTION]注意
> `count`须不大于预约句柄名下的资源个数。`count`为0时直接构造成空Array（`size() == 0`），不绑定资源，也不校验`acq_handle`或当前是否处于kernel注册阶段。

## 成员函数说明

| 函数 | 说明 |
| --- | --- |
| `arr[i]` | 返回第`i`个元素的引用（从0起，无边界检查）。 |
| `arr.data()` | 返回首元素指针，用于传给需要`T*`参数的批量接口。 |
| `arr.size()` | 返回申请时的`count`值。 |

## 约束说明

- `array(count)`以及`count > 0`的`array(acq_handle, count)`只能在kernel注册阶段构造。
- 析构不释放硬件资源，不应在kernel之外保存元素的`handle`值，翻译完成后句柄即失效。
- `array<T>`仅特化`variable/event/ccu_buffer`三种类型，对其他类型实例化将在编译期失败。

- `array(count)`只申请虚拟句柄，恒成功；资源池无法凑出`n`个连续物理资源时，在`HcommCcuKernelRegister`阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- `array(acq_handle, count)`在`count > 0`时会校验预约句柄与`count`，参数不合法时在构造时抛出异常，不再保证恒成功。

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    // 批量申请4个物理连续variable
    array<variable> vArr(4);

    // 批量申请4个物理连续ccu_buffer
    array<ccu_buffer> bufs(4);

    // 下标访问单个元素
    vArr[0] = 1024;    // 对第0个variable赋立即数

    return CCU_SUCCESS;
}
```

绑定Host侧预约资源的用法：

```cpp
using namespace AscendC::ccu;

// Host侧通过HcommCcuVariableAlloc / HcommCcuEventAlloc预约资源后，
// 把预约句柄和个数放进kernelArgs传入kernel
struct my_kernel_arg {
    ccu_variable_handle acq_handle;
    uint32_t var_num;
    ccu_event_handle acq_event_handle;
    uint32_t event_num;
};

CcuResult my_kernel(ccu_kernel_arg arg) {
    auto* args = static_cast<my_kernel_arg*>(arg);

    // 绑定Host侧预约的var_num个标量寄存器，不申请新的标量寄存器；绑定后的variable用法与
    // 普通variable完全相同，这里仅以逐个赋值演示下标访问
    array<variable> vars(args->acq_handle, args->var_num);
    for (uint32_t i = 0; i < vars.size(); i++) {
        vars[i] = i;
    }

    // 绑定Host侧预约的event_num个event单元；本例在CCU侧event_record
    array<event> events(args->acq_event_handle, args->event_num);
    for (uint32_t i = 0; i < events.size(); i++) {
        event_record(events[i]);
    }

    return CCU_SUCCESS;
}
```
