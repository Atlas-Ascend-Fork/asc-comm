# array

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

`ccu::array<T>`是CCU kernel内批量持有物理连续资源的C++模板类。

- 构造即批量分配：`array(count)`一次性申请`count`个物理连续的虚拟句柄。
- `array(acq_handle, count)`绑定Host侧已预约的前`count`个资源，不申请新的物理资源。
- 析构不释放：析构函数不释放硬件资源；虚拟句柄在翻译完成后失效，物理资源随CCU实例生命周期统一管理、回收。
- 仅可移动：禁止拷贝，允许移动。

## 类声明

```cpp
namespace AscendC {
namespace ccu {
template <typename T>  // T仅支持variable、event、ccu_buffer
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

## 参数说明

当前仅支持以下三种特化类型：

| 特化类型 | 资源描述 |
| --- | --- |
| `array<variable>` | N个物理连续标量寄存器 |
| `array<event>` | N个物理连续event单元 |
| `array<ccu_buffer>` | N个物理连续CCU Buffer |

## 构造函数说明

- 批量构造

    | 构造形式 | 说明 |
    | --- | --- |
    | `array<variable> vars(count);` | 申请`count`个物理连续标量寄存器句柄。 |
    | `array<event> evts(count);` | 申请`count`个物理连续event单元句柄。 |
    | `array<ccu_buffer> bufs(count);` | 申请`count`个物理连续CCU Buffer句柄。 |

- 预约构造

    | 构造形式 | 说明 |
    | --- | --- |
    | `array<variable> vars(acq_handle, count);` | 绑定Host侧预约的前`count`个标量寄存器，不申请新的标量寄存器。 |
    | `array<event> evts(acq_handle, count);` | 绑定Host侧预约的前`count`个event单元，不申请新的event单元。 |

    构造函数中的`acq_handle`参数类型须与`T`对应：
    - `array<variable>`用`asccomm_ccu_variable_alloc`返回的预约句柄；
    - `array<event>`用`asccomm_ccu_event_alloc`返回的预约句柄。

    构造时依次绑定预约段内序号`0`到`count - 1`的资源，元素`arr[i]`对应预约段内的第`i`个资源，与`asccomm_ccu_variable_get_addr`、`asccomm_ccu_event_get_addr`中的`index`一一对应。

## 成员函数说明

| 函数 | 说明 |
| --- | --- |
| `arr[i]` | 返回第`i`个元素的引用（从0起，无边界检查）。 |
| `arr.data()` | 返回首元素指针，用于传给需要`T*`参数的批量接口。 |
| `arr.size()` | 返回申请时的`count`值。 |

## 约束说明

- `array(count)`和`array(acq_handle, count)`只能在注册阶段构造。
- `array(count)`恒成功；资源池无法凑出`count`个连续物理资源时，由注册阶段返回`CCU_E_UNAVAIL`，不是在构造时抛出。
- `array<T>`仅特化`variable`、`event`、`ccu_buffer`三种类型；预约构造只对`variable`、`event`可用，其他类型实例化或使用预约构造会在编译期失败。
- `array(acq_handle, count)`在`count > 0`时会校验预约句柄与`count`，参数不合法时抛出异常，被`<<<>>>`统一接住，对外返回`CCU_E_INTERNAL`，本轮注册被中止。
- `count`须不大于预约句柄名下的资源个数；`count`为0时直接构造成空`array`（`size() == 0`），不绑定资源，也不校验`acq_handle`。
- 不应在kernel之外保存元素的`handle`值，翻译完成后句柄即失效。

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult my_kernel(ccu_kernel_arg arg) {
    // 批量申请4个物理连续variable
    array<variable> v_arr(4);

    // 批量申请4个物理连续ccu_buffer
    array<ccu_buffer> bufs(4);

    // 下标访问单个元素
    v_arr[0] = 1024;    // 对第0个variable赋立即数

    return CCU_SUCCESS;
}
```

绑定Host侧预约资源的用法：

```cpp
using namespace AscendC::ccu;

// Host侧通过asccomm_ccu_variable_alloc/asccomm_ccu_event_alloc预约资源后，
// 把预约句柄和个数放进kernelArg传入kernel
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
