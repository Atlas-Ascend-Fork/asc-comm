# Unlock

## 功能说明

发布指定通信通道的状态并释放跨AI Core锁。接口仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径。

释放锁前，接口会写回并失效通道counter所在的DataCache缓存行，并执行DDR数据同步屏障，确保受保护的通道状态先于锁释放对后续持锁AI Core可见。

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
| `-1` | 释放锁失败。`channel`为空、未满足通道实体对齐要求，或锁在释放前不是HELD状态。 |

## 约束说明

- 仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`路径。
- 只能在当前AI Core已成功调用[Lock](./Lock.md)后调用`Unlock`，并且需要由获取锁的同一AI Core释放。
- 实现不记录或校验锁的owner。由未获取锁的AI Core调用`Unlock`可能释放其他AI Core持有的锁并破坏互斥，调用方必须保证配对关系正确。
- 不得重复调用`Unlock`。锁不是HELD状态时接口返回`-1`。
- BatchHandle会在创建时缓存队列游标。对单次批量操作加锁不能使多个BatchHandle并发或普通接口与批量接口混用变得安全；BatchHandle在整个生命周期内仍必须独占对应通道或共享Jetty。
- cache write-through仅覆盖通道的`sqHead`、`sqTail`、`cqHead`和`cqTail`所在缓存行，不负责刷新任意用户GM地址。该接口不能作为任意GM数据的通用发布操作。
- 该锁只用于同步同一设备上共享同一通道的AI Core，不提供跨Rank或跨设备同步。
