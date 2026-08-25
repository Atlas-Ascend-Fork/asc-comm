# MakeMultiChannelHandleWithComm

## 功能说明

基于HCCL通信域创建一组共享Jetty的UB_CTP通信通道，并创建Device侧使用的`MultiChannelHandle`。接口按照`channelDescs`顺序保存各逻辑通道的远端通信信息。

头文件为：

```cpp
#include "hcomm/hcomm_host.h"
```

## 函数原型

```cpp
HcclResult MakeMultiChannelHandleWithComm(
    HcclComm comm,
    const char* sharedQueueTag,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `comm` | 输入 | 已初始化的HCCL通信域。共享Jetty通道和Device上下文由该通信域管理。 |
| `sharedQueueTag` | 输入 | 本组共享队列及Device上下文的标签，在同一通信域内必须唯一。 |
| `channelDescs` | 输入 | 包含`channelNum`个元素的HCCL通道描述数组。数组顺序决定`GetHandleRef`使用的`channelIndex`。 |
| `channelNum` | 输入 | 通道描述数量，必须大于0。 |
| `multiChannel` | 输出 | Device侧多通道句柄。创建失败时置为0。 |

## 返回值

成功返回`HCCL_SUCCESS`，失败返回对应的HCCL错误码。

## 约束说明

- 通道使用`COMM_ENGINE_AIV`和UB_CTP协议，并满足HCCL共享队列通道约束。
- 同一通信域内，每个`sharedQueueTag`仅用于一次成功调用。
- 句柄及其Device上下文由`comm`管理，无需调用`DestroyMultiChannelHandle`。Kernel使用完成前不能销毁通信域。
- 无通信域路径请使用`MakeMultiChannelHandle`。

## 相关接口

- [MakeMultiChannelHandle](MakeMultiChannelHandle.md)
- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
