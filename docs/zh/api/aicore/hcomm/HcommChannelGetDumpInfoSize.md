# HcommChannelGetDumpInfoSize

## 功能说明
返回接收一次通道dump原始快照所需的缓冲区字节数。调用方据此分配内存，再把该缓冲区（`char *`）传给[HcommChannelInfoDump](./HcommChannelInfoDump.md)。

## 函数原型
```cpp
uint32_t HcommChannelGetDumpInfoSize();
```

## 参数说明
无。

## 返回值
| 返回值 | 说明 |
| --- | --- |
| `>0` | 一次dump原始快照所需的字节数。 |

## 约束说明
- 传给`HcommChannelInfoDump`的缓冲区长度必须不小于本接口返回值。
- 返回值在同一次构建内稳定；升级库版本后需重新获取，不要硬编码。
- 快照以字节形式返回，本接口只提供大小，不暴露内部数据结构布局。
