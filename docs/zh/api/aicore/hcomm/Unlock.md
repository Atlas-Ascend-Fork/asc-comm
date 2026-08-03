# Unlock

## 功能说明

释放通过[Lock](./Lock.md)获取的通信通道锁。

## 函数原型

```cpp
__aicore__ inline int32_t Unlock(AscendC::ChannelHandle channel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 释放锁成功。 |
| `-1` | 释放锁失败。 |

## 约束说明

- 仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`路径。
- 仅在[Lock](./Lock.md)成功后调用`Unlock`，并由获取锁的同一AI Core释放。
- 同一次锁获取不得重复调用`Unlock`。
- 仅用于同步同一设备上共享同一通道的AI Core。
