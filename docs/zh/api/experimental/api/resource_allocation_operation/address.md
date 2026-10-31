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

`__ccu_addr`将设备地址和通信token组成CCU地址能力。普通编译环境使用`__ccu_var_t` token；`ccuc`还提供`__ccu_token_t`重载；`__ccu_addr_offset`在保留token的
同时生成字节偏移后的地址。

## 函数原型

```cpp
__ccu_addr_t __ccu_addr(__ccu_var_t address, __ccu_var_t token);
#ifdef __CCUC__
__ccu_addr_t __ccu_addr(__ccu_var_t address, __ccu_token_t token);
#endif
__ccu_addr_t __ccu_addr_offset(__ccu_addr_t base,
                               __ccu_var_t byte_offset);
```

## 约束说明

- `address`与`token`必须对应同一段已注册通信内存；`token`是访问凭证，不能由地址推导。
- offset必须落在Host声明的buffer范围内。
- 地址类型不是普通指针，不能取地址、转换成指针或跨Host ABI传递。

## 调用示例

```cpp
__ccu_addr_t input = __ccu_addr(input_address, token);
__ccu_addr_t tail = __ccu_addr_offset(input, byte_offset);
__ccu_local_copy_nb(output, tail, bytes, done, 1);
```
