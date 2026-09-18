# Lock

## 功能说明

多个AI Core共享同一通信通道时，获取该通道的锁。`channel`有效时，如果锁已被持有，则等待直至成功获取锁，不会因锁竞争返回失败。

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
| `-1` | `channel`为空或地址未对齐。锁竞争不会返回`-1`。 |

## 约束说明

- 仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`路径。
- 锁不可重入。释放锁前，不得对同一通道再次调用`Lock`。
- 每次成功调用`Lock`后，都必须由同一AI Core调用[Unlock](./Unlock.md)，错误退出路径也需要释放锁。
- 仅用于同步同一设备上共享同一通道的AI Core。
