# 地址

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

头文件为：`#include <ccu_intrinsics.h>`

`__ccu_addr_t`表示CCU kernel内用于访问通信内存的地址，由设备地址和配套token组成。
调用`__ccu_addr`可构造该地址；数据搬运和变量读写接口使用该地址访问通信内存。

## 函数原型

```cpp
__ccu_addr_t __ccu_addr(__ccu_var_t address, __ccu_var_t token);
__ccu_addr_t __ccu_addr(__ccu_var_t address, __ccu_token_t token);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `address` | 输入 | `__ccu_var_t` | 设备内存地址 |
| `token` | 输入 | `__ccu_var_t` / `__ccu_token_t` | 与地址配套的通信内存token |

## 约束说明

- `address`与`token`必须来自同一段通信内存。
- `__ccu_addr_t`只能在CCU kernel内使用，不能取地址或转换为普通指针。

## 调用示例

```cpp
__ccu_addr_t input = __ccu_addr(input_address, token);
__ccu_local_copy_nb(output, input, bytes, done, 1);
```
