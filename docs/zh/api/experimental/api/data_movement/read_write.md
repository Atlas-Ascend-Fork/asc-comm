# 远端读写

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

读写接口由本端发起，并通过channel访问远端内存：

- `write`：本端数据写入远端内存，`dst`为远端地址，`src`为本端地址或本端buf。
- `read`：本端向远端发起读请求，`dst`为本端地址或本端buf，`src`为远端地址；归约读会在本端目标执行`sum`、`max`或`min`。

地址使用统一的`__ccu_addr_t`表达；本地或远端语义由接口和参数位置决定，不由类型本身区分。

## 函数原型

```cpp
void __ccu_write_nb(__ccu_channel_t peer, __ccu_addr_t dst,
                    __ccu_addr_t src, __ccu_var_t length,
                    __ccu_event_t event, uint16_t mask = 1);
void __ccu_read_nb(__ccu_channel_t peer, __ccu_addr_t dst,
                   __ccu_addr_t src, __ccu_var_t length,
                   __ccu_event_t event, uint16_t mask = 1);
void __ccu_write_from_buf_nb(__ccu_channel_t peer, __ccu_addr_t dst,
                             __ccu_buf_block_t src, __ccu_var_t length,
                             __ccu_event_t event, uint16_t mask = 1);
void __ccu_read_to_buf_nb(__ccu_channel_t peer, __ccu_buf_block_t dst,
                          __ccu_addr_t src, __ccu_var_t length,
                          __ccu_event_t event, uint16_t mask = 1);

void __ccu_read_reduce_sum_nb(__ccu_channel_t peer,
                              __ccu_addr_t dst, __ccu_addr_t src,
                              __ccu_var_t length, __ccu_dtype_t dtype,
                              __ccu_event_t event, uint16_t mask = 1);
void __ccu_read_reduce_max_nb(__ccu_channel_t peer,
                              __ccu_addr_t dst, __ccu_addr_t src,
                              __ccu_var_t length, __ccu_dtype_t dtype,
                              __ccu_event_t event, uint16_t mask = 1);
void __ccu_read_reduce_min_nb(__ccu_channel_t peer,
                              __ccu_addr_t dst, __ccu_addr_t src,
                              __ccu_var_t length, __ccu_dtype_t dtype,
                              __ccu_event_t event, uint16_t mask = 1);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `peer` | 输入 | `__ccu_channel_t` | 对端channel，来自`__ccu_channels[index]`；`index`必须是编译期常量 |
| `dst` | 输入 | `__ccu_addr_t`或`__ccu_buf_block_t` | 写接口为远端目的地址；读接口为本端目的地址或本端buf |
| `src` | 输入 | `__ccu_addr_t`或`__ccu_buf_block_t` | 写接口为本端源地址或本端buf；读接口为远端源地址 |
| `length` | 输入 | `__ccu_var_t` | 搬运字节数 |
| `event` | 输入 | `__ccu_event_t` | 完成事件；搬运完成后置位指定mask |
| `mask` | 输入 | `uint16_t` | 完成事件signal mask，默认`1`；必须是编译期常量 |
| `dtype` | 输入 | `__ccu_dtype_t` | 归约元素类型；必须是编译期枚举常量 |

## 返回值说明

接口返回`void`。参数非法或资源约束不满足时，由编译器诊断。

## 约束说明

- `peer`必须来自`__ccu_channels[index]`，且下标必须是编译期常量。
- channel下标必须小于当前kernel的peer channel数量。
- 远端地址只能访问对应channel关联的通信内存，不能从本端地址推导。
- `mask`必须是16位编译期常量。
- 归约读的`dtype`必须是编译期枚举常量。
- 搬运是异步的；读取或覆盖目标前必须调用`__ccu_event_wait`。

## 调用示例

```cpp
// 1. 获取第一个对端 channel。
__ccu_channel_t peer = __ccu_channels[0];

// 2. 从 channel 关联的远端地址和访问 token 构造远端地址。
//    [1] 对应该 channel 的远端地址，[2] 对应该地址的访问 token。
__ccu_addr_t remote = __ccu_addr(__ccu_channel_xns[0][1],
                                 __ccu_channel_xns[0][2]);

// 3. 声明完成事件。
__ccu_event_t done;

// 4. 从远端读取数据到本端。
__ccu_read_nb(peer, local_dst, remote, bytes, done, 1);

// 5. 等待读完成后再访问目标数据。
__ccu_event_wait(done, 1);
```
