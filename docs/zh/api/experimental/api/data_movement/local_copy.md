# 本地拷贝

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

在本卡内存和本卡CCU Buffer之间执行异步数据搬运或本地归约。归约操作由函数名固定为`sum`、`max`或`min`。

## 函数原型

```cpp
void __ccu_local_copy_nb(__ccu_addr_t dst, __ccu_addr_t src,
                         __ccu_var_t length, __ccu_event_t event,
                         uint16_t mask = 1);
void __ccu_local_copy_to_buf_nb(__ccu_buf_block_t dst, __ccu_addr_t src,
                                __ccu_var_t length, __ccu_event_t event,
                                uint16_t mask = 1);
void __ccu_local_copy_from_buf_nb(__ccu_addr_t dst, __ccu_buf_block_t src,
                                  __ccu_var_t length, __ccu_event_t event,
                                  uint16_t mask = 1);
void __ccu_local_copy_buf_to_buf_nb(__ccu_buf_block_t dst,
                                    __ccu_buf_block_t src,
                                    __ccu_var_t length, __ccu_event_t event,
                                    uint16_t mask = 1);

void __ccu_local_reduce_sum_nb(__ccu_addr_t dst,
                               __ccu_addr_t src, __ccu_var_t length,
                               __ccu_dtype_t dtype, __ccu_event_t event,
                               uint16_t mask = 1);
void __ccu_local_reduce_max_nb(__ccu_addr_t dst,
                               __ccu_addr_t src, __ccu_var_t length,
                               __ccu_dtype_t dtype, __ccu_event_t event,
                               uint16_t mask = 1);
void __ccu_local_reduce_min_nb(__ccu_addr_t dst,
                               __ccu_addr_t src, __ccu_var_t length,
                               __ccu_dtype_t dtype, __ccu_event_t event,
                               uint16_t mask = 1);

void __ccu_local_buf_reduce_sum_nb(const __ccu_buf_block_t buffers[],
                                   uint32_t buffer_count, __ccu_var_t length,
                                   __ccu_dtype_t input_dtype,
                                   __ccu_dtype_t output_dtype,
                                   __ccu_event_t event, uint16_t mask = 1);
void __ccu_local_buf_reduce_max_nb(const __ccu_buf_block_t buffers[],
                                   uint32_t buffer_count, __ccu_var_t length,
                                   __ccu_dtype_t input_dtype,
                                   __ccu_event_t event, uint16_t mask = 1);
void __ccu_local_buf_reduce_min_nb(const __ccu_buf_block_t buffers[],
                                   uint32_t buffer_count, __ccu_var_t length,
                                   __ccu_dtype_t input_dtype,
                                   __ccu_event_t event, uint16_t mask = 1);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `dst` | 输入 | `__ccu_addr_t`或`__ccu_buf_block_t` | 目的地址或目的buf block，类型由具体接口决定 |
| `src` | 输入 | `__ccu_addr_t`或`__ccu_buf_block_t` | 源地址或源buf block，类型由具体接口决定 |
| `length` | 输入 | `__ccu_var_t` | 搬运字节数 |
| `event` | 输入 | `__ccu_event_t` | 完成事件；搬运完成后置位指定mask |
| `mask` | 输入 | `uint16_t` | 完成事件signal mask，默认`1`；必须是编译期常量 |
| `dtype` / `input_dtype` | 输入 | `__ccu_dtype_t` | 归约输入元素类型；必须是编译期枚举常量 |
| `output_dtype` | 输入 | `__ccu_dtype_t` | 仅sum多buf归约接口使用；当前必须与输入dtype相同 |
| `buffers` | 输入 | `const __ccu_buf_block_t[]` | 参与归约的本端buf block数组 |
| `buffer_count` | 输入 | `uint32_t` | 参与归约的buf block数量；必须是2到8之间的编译期常量 |

## 返回值说明

接口返回`void`。非法地址、Buffer、长度或mask由编译器诊断。

## 约束说明

- 接口是异步的；访问目标前必须调用`__ccu_event_wait`。
- 源和目标地址区间不得重叠。
- `length`不能超过源和目标可访问范围；buf参与时不能超过目标设备定义的单个buf block容量。
- `mask`必须是16位编译期常量。
- 归约接口的`dtype` / `input_dtype`必须是编译期枚举常量。
- `__ccu_local_buf_reduce_sum_nb`的`output_dtype`当前必须与`input_dtype`相同。
- 多buf CcuBuf归约当前不支持`CCU_DTYPE_UINT16`和`CCU_DTYPE_UINT32`。
- buf归约接口的`buffer_count`必须是编译期常量。
- 不要在kernel中自行计算或填写资源编号。

## 调用示例

```cpp
// 1. 声明完成事件。
__ccu_event_t done;

// 2. 发起本卡内存到内存的异步拷贝。
__ccu_local_copy_nb(dst, src, bytes, done, 1);

// 3. 等待拷贝完成后再访问目标数据。
__ccu_event_wait(done, 1);

// 也可对本地内存执行 FP32 sum 归约。
__ccu_local_reduce_sum_nb(dst, src, bytes, CCU_DTYPE_FP32, done, 1);
```
