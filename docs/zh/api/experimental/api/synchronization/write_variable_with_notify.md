# Variable写入并通知

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

将本端Variable值写入对端指定Variable槽位，同时对对端指定notify槽位置位。

## 函数原型

```cpp
void __ccu_write_variable_with_notify(
    __ccu_channel_t channel, __ccu_var_t value,
    uint16_t remote_var_idx, uint16_t remote_notify_idx,
    uint16_t mask);

// ccuc also accepts __ccu_token_t for value.
void __ccu_write_variable_with_notify(
    __ccu_channel_t channel, __ccu_token_t value,
    uint16_t remote_var_idx, uint16_t remote_notify_idx,
    uint16_t mask);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `channel` | 输入 | `__ccu_channel_t` | 对端channel，来自`__ccu_channels[index]` |
| `value` | 输入 | `__ccu_var_t` / `__ccu_token_t` | 要写入远端的值 |
| `remote_var_idx` | 输入 | `uint16_t` | 远端Variable逻辑槽位编号；编译期常量 |
| `remote_notify_idx` | 输入 | `uint16_t` | 远端notify槽位编号；编译期常量 |
| `mask` | 输入 | `uint16_t` | 远端notify signal mask；编译期常量 |

## 返回值说明

接口返回`void`。非法槽位、mask或channel使用由编译器诊断。

## 约束说明

- `remote_var_idx`、`remote_notify_idx`和`mask`必须是编译期常量。
- 当前产品的remote variable index必须小于`3072`。
- 当前产品的remote notify index必须小于`1024`。
- `mask`必须适配16位立即数。
- 该同步写入接口不得放在受限循环body中。
- 接收端应先`__ccu_notify_wait`，再消费对应Variable。

## 调用示例

```cpp
// 1. 将本端 output_base_address 写入对端 Variable 槽位 1，
//    同时通知对端 notify 槽位 0 的 bit0。
__ccu_write_variable_with_notify(__ccu_channels[0], output_base_address,
                                 1, 0, 1);

// 2. 接收端等待本端 notify 槽位 0 的 bit0。
__ccu_notify_wait(__ccu_channels[0], 0, 1);
```
