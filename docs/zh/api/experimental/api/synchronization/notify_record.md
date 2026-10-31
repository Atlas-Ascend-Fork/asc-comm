# Notify记录

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

通过peer channel通知对端，将对端指定notify槽位的signal mask置位。`event_idx`表示对端notify槽位编号，不是本端槽位编号。

## 函数原型

```cpp
void __ccu_notify_set(__ccu_channel_t channel, uint16_t event_idx,
                      uint16_t mask);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `channel` | 输入 | `__ccu_channel_t` | 对端channel，来自`__ccu_channels[index]` |
| `event_idx` | 输入 | `uint16_t` | 对端notify槽位编号；必须是编译期常量 |
| `mask` | 输入 | `uint16_t` | 要置位的对端signal mask；必须是编译期常量 |

## 返回值说明

接口返回`void`。非法channel、event index或mask由编译器诊断。

## 约束说明

- `event_idx`和`mask`必须是编译期常量。
- `event_idx`是对端槽位编号，需要与接收端`__ccu_notify_wait`使用的本端槽位一致。
- `0x0` mask表示不通知任何signal。
- channel和notify槽位都是逻辑资源。

## 调用示例

```cpp
// 通过第一个对端 channel，通知对端 notify 槽位 0 的 bit0。
__ccu_notify_set(__ccu_channels[0], 0, 1);
```
