# ccu_address_handle

## 功能说明

头文件为：`#include <ccu/asc_ccu.h>`

`ccu_address_handle`是CCU address相关的句柄类型（`uint64_t`），用于标识Kernel函数体内通过[address](../device/resource_allocation_operation/address.md)资源创建的地址寄存器。该句柄作为`address`的`handle`成员，仅在当前Kernel内有效。

## 类型定义

`ccu_address_handle`在`ccu/ccu_api/ccu_types.h`中定义：

```c
typedef uint64_t ccu_address_handle;
```

## 用法说明

句柄由`address`在Kernel函数体内的默认构造产生（`address`仅有此一种构造形式），物理资源在翻译期统一分配。句柄值不作为任何接口的独立入参使用，仅随`address`对象参与数据面接口。

## 约束说明

- 不应在Kernel之外保存或比较`handle`值，翻译完成后句柄即失效。
- `address`无预约构造形式，`ccu_address_handle`不与Host侧资源预约接口交互。
