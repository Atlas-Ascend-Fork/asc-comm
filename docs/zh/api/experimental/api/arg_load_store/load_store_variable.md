# 变量加载与存储

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

该接口在CCU kernel内部对带token的地址执行一次同步标量加载或存储。每次操作只传输
一个CCU variable word。

## 函数原型

```cpp
void __ccu_load_variable(__ccu_var_t dst, __ccu_addr_t src);
void __ccu_store_variable(__ccu_addr_t dst, __ccu_var_t src);
```

### __ccu_load_variable参数

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `dst` | 输出 | `__ccu_var_t` | 接收加载结果的CCU variable |
| `src` | 输入 | `__ccu_addr_t` | 带token的源地址 |

### __ccu_store_variable参数

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `dst` | 输入 | `__ccu_addr_t` | 带token的目标地址 |
| `src` | 输入 | `__ccu_var_t` | 要存储的变量值 |

## 约束说明

- 只能在CCU kernel中调用，不能在Host侧调用。
- 地址必须由`__ccu_addr`创建，并携带与通信buffer匹配的token。
- 变量必须在使用前完成初始化；不要把变量地址转换为普通指针。

## 调用示例

```cpp
__ccu_var_t value;
__ccu_addr_t metadata = __ccu_addr(metadata_address, token);
__ccu_load_variable(value, metadata);
__ccu_store_variable(__ccu_addr(result_address, token), value);
```
