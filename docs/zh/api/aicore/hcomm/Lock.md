# Lock

## 功能说明

获取指定通信通道的跨AI Core锁，用于多个AI Core共享同一通道时保护通道状态。接口仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径。

如果锁已被其他AI Core持有，接口会持续自旋，直到成功获取锁。

## 函数原型

```cpp
__aicore__ inline int32_t Lock(AscendC::ChannelHandle channel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 获取锁成功。 |
| `-1` | 获取锁失败。`channel`为空或不满足通道实体对齐要求。 |

## 约束说明

- 仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`路径。
- `channel`需要指向已正确初始化的通信通道，锁字段初始状态需要为FREE。
- 接口没有超时机制，也不保证多个AI Core之间的获取顺序。锁被长期占用时，调用方会一直自旋等待。
- 锁不可重入。成功调用`Lock`后，不得在同一AI Core上对同一通道再次调用`Lock`。
- 每次成功调用`Lock`后，都必须由同一AI Core调用[Unlock](./Unlock.md)。获取锁后的所有退出路径均需要释放锁。
- 多个AI Core共享同一通道时，所有会更新通道状态的访问都需要置于`Lock`和`Unlock`之间。
- BatchHandle会在创建时缓存队列游标。对单次批量操作加锁不能使多个BatchHandle并发或普通接口与批量接口混用变得安全；BatchHandle在整个生命周期内仍必须独占对应通道或共享Jetty。
- 该锁只用于同步同一设备上共享同一通道的AI Core，不提供跨Rank或跨设备同步。
- 接口成功获取锁后执行DDR数据同步屏障，使前一个持锁AI Core通过`Unlock`发布的通道状态对当前AI Core可见。
