# Variable

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

`__ccu_var_t`是CCU kernel内的标量资源类型，用于保存task参数、地址、长度、
索引和控制值。它不是Host侧的C/C++ 数值类型，也不是普通指针。

## 使用约束

- 变量必须在读取前完成初始化。
- 只允许受支持的赋值、加法、比较和编译期有界控制流。
- 不支持VLA、runtime下标、取地址、reinterpret cast和动态分配。
- 需要从通信内存读取多个64位字时，使用[变量加载与存储](../arg_load_store/load_store_variable.md)。

## 调用示例

```cpp
__ccu_var_t offset = 0;
if (offset != 0)
  __ccu_local_copy_nb(output, input, offset, done, 1);
```
