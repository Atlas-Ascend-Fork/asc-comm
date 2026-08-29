# Unlock

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->

## 功能说明

刷新通道状态并释放通过[Lock](./Lock.md)获取的通信通道跨AI Core锁。释放前将通道的队列计数（`sqHead`、`sqTail`、`cqHead`、`cqTail`）写穿到GM，确保其他AI Core可见。

## 函数原型

```cpp
__aicore__ inline int32_t Unlock(AscendC::ChannelHandle channel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 释放锁成功。 |
| `-1` | `channel`为空或地址未对齐，或锁未被持有（如未获取锁或重复释放）。 |

## 约束说明

- 仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`路径。
- 仅在[Lock](./Lock.md)成功后调用`Unlock`，并由获取锁的同一AI Core释放。
- 需要在最后一次更新通道状态的操作之后调用`Unlock`，保证通道状态刷新后对其他AI Core可见。
- 同一次锁获取不得重复调用`Unlock`。
- 仅用于同步同一设备上共享同一通道的AI Core。

## 调用示例

Unlock接口的调用示例如下。

```cpp
hcomm.Lock(channel);
// 更新通道状态的操作
hcomm.Unlock(channel);    // 刷新通道状态并释放锁
```
