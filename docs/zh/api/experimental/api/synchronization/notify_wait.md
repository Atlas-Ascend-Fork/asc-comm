# Notify等待

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

等待对端通过同一channel发送的notify。`event_idx`表示本端notify槽位编号，需要与对端`__ccu_notify_set`传入的对端槽位编号一致。

## 函数原型

```cpp
void __ccu_notify_wait(__ccu_channel_t channel, uint16_t event_idx,
                       uint16_t mask);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `channel` | 输入 | `__ccu_channel_t` | 对端channel，来自`__ccu_channels[index]` |
| `event_idx` | 输入 | `uint16_t` | 本端notify槽位编号；必须是编译期常量 |
| `mask` | 输入 | `uint16_t` | 等待的本端signal mask；必须是编译期常量 |

## 返回值说明

接口返回`void`。非法channel、event index或mask由编译器诊断。

## 约束说明

- `event_idx`和`mask`必须是编译期常量。
- `event_idx`是本端槽位编号。
- 发送端和接收端必须使用一致的逻辑槽位编号和mask。
- 双方必须使用同一通信域提供的channel。

## 调用示例

```cpp
// 1. 等待对端写入本端 notify 槽位 0 的 bit0。
__ccu_notify_wait(__ccu_channels[0], 0, 1);

// 2. 通知到达后，再读取远端数据。
__ccu_read_nb(__ccu_channels[0], local_dst, remote_src, bytes, done, 1);

// 3. 等待读取完成。
__ccu_event_wait(done, 1);
```
