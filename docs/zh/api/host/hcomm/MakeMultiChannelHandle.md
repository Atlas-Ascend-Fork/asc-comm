# MakeMultiChannelHandle

## 功能说明

创建一组共享Jetty的UB_CTP通信通道及Device侧使用的`MultiChannelHandle`。接口提供HCCL通信域和Hcomm Endpoint两种重载，并按照`channelDescs`顺序保存各逻辑通道的远端通信信息。

头文件为：

```cpp
#include "hcomm/hcomm_host.h"
```

## 函数原型

```cpp
HcclResult MakeMultiChannelHandle(
    HcclComm comm,
    const char* sharedQueueTag,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);

HcommResult MakeMultiChannelHandle(
    EndpointHandle endpointHandle,
    HcommChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `comm` | 输入 | 已初始化的HCCL通信域。共享Jetty通道和Device上下文由该通信域管理。 |
| `sharedQueueTag` | 输入 | 本组共享队列及Device上下文的标签，在同一通信域内必须唯一。 |
| `endpointHandle` | 输入 | 已初始化的Hcomm Endpoint。该Endpoint必须支持AIV引擎的UB_CTP协议。 |
| `channelDescs` | 输入 | 包含`channelNum`个元素的HCCL或Hcomm通道描述数组。数组顺序决定`GetHandleRef`使用的`channelIndex`。 |
| `channelNum` | 输入 | 通道描述数量，必须大于0。 |
| `multiChannel` | 输出 | Device侧多通道句柄。创建失败时置为0。 |

## 返回值

成功返回`HCCL_SUCCESS`。通信域重载失败时返回对应的HCCL错误码；Endpoint重载失败时返回对应的Hcomm错误码。Endpoint重载在建链失败、资源不足或120秒内未全部就绪时销毁已创建的通道并返回错误。

## 约束说明

- 两种重载的输入指针均不能为`nullptr`，`channelNum`不能为0。
- 通信域重载使用`HcclChannelAcquireWithConfig`创建通道。同一通信域内，每个`sharedQueueTag`仅用于一次成功调用；共享通道和Device上下文由`comm`管理，并在通信域销毁时自动释放，用户不能调用`DestroyMultiChannelHandle`释放该句柄。
- Endpoint重载使用`HcommChannelCreateWithConfig`创建通道并等待建链就绪。返回句柄拥有新建的通道和Device上下文，必须在销毁`endpointHandle`前调用对应的`DestroyMultiChannelHandle`重载。
- 通道使用`COMM_ENGINE_AIV`和UB_CTP协议。共享Jetty通道不能并发使用。
- Endpoint重载创建的句柄在Kernel使用并同步完成后，必须调用`DestroyMultiChannelHandle`释放。

## 相关接口

- [DestroyMultiChannelHandle](DestroyMultiChannelHandle.md)
- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
