# 远端归约写

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

将本端数据写入远端目标地址，并在写入远端内存时执行element-wise归约。归约操作由函数名固定为`sum`、`max`或`min`，不作为运行时或编译期枚举参数传入。

## 函数原型

```cpp
void __ccu_write_reduce_sum_nb(__ccu_channel_t peer, __ccu_addr_t dst,
                               __ccu_addr_t src, __ccu_var_t length,
                               __ccu_dtype_t dtype, __ccu_event_t event,
                               uint16_t mask = 1);
void __ccu_write_reduce_max_nb(__ccu_channel_t peer, __ccu_addr_t dst,
                               __ccu_addr_t src, __ccu_var_t length,
                               __ccu_dtype_t dtype, __ccu_event_t event,
                               uint16_t mask = 1);
void __ccu_write_reduce_min_nb(__ccu_channel_t peer, __ccu_addr_t dst,
                               __ccu_addr_t src, __ccu_var_t length,
                               __ccu_dtype_t dtype, __ccu_event_t event,
                               uint16_t mask = 1);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `peer` | 输入 | `__ccu_channel_t` | 对端channel，来自`__ccu_channels[index]` |
| `dst` | 输入 | `__ccu_addr_t` | 远端目的地址 |
| `src` | 输入 | `__ccu_addr_t` | 本端源地址 |
| `length` | 输入 | `__ccu_var_t` | 搬运和归约的数据长度，单位为字节 |
| `dtype` | 输入 | `__ccu_dtype_t` | 元素数据类型，必须是编译期枚举常量 |
| `event` | 输入 | `__ccu_event_t` | 完成事件 |
| `mask` | 输入 | `uint16_t` | 完成事件signal mask，默认`1` |

当前Ascend 950PR&950DT系列产品支持以下dtype：

```text
CCU_DTYPE_INT8
CCU_DTYPE_INT16
CCU_DTYPE_INT32
CCU_DTYPE_FP16
CCU_DTYPE_FP32
CCU_DTYPE_BF16
CCU_DTYPE_UINT8
CCU_DTYPE_UINT16
CCU_DTYPE_UINT32
```

## 返回值说明

接口返回`void`。不支持的dtype、长度、对齐或目标地址组合由编译器诊断。

## 约束说明

- `dtype`必须是编译期枚举常量。
- `length`、源地址、目标地址和dtype必须满足目标设备约束和对齐要求。
- 目标必须是peer channel可访问的合法远端内存地址。
- 接口为异步接口；完成后通过`event/mask`通知后续操作。
- 访问归约结果前必须调用`__ccu_event_wait`。

## 调用示例

```cpp
// 1. 声明完成事件。
__ccu_event_t done;

// 2. 将本端数据写入远端目标，并执行 FP32 sum 归约。
__ccu_write_reduce_sum_nb(__ccu_channels[0], remote_dst, local_src,
                          bytes, CCU_DTYPE_FP32, done, 1);

// 3. 等待归约写完成。
__ccu_event_wait(done, 1);
```
