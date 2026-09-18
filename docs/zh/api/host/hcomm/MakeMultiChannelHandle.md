# MakeMultiChannelHandle

## 功能说明

创建批量接口多通道模式所需的通信资源，并返回Device侧使用的`MultiChannelHandle`。接口提供HCCL通信域和Hcomm Endpoint两种重载。

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
| `comm` | 输入 | 已初始化的HCCL通信域。 |
| `sharedQueueTag` | 输入 | 本组多通道资源的标签，在同一通信域内必须唯一。 |
| `endpointHandle` | 输入 | 已初始化且支持AIV引擎UB_CTP协议的Hcomm Endpoint。 |
| `channelDescs` | 输入 | 包含`channelNum`个元素的HCCL或Hcomm通道描述数组。数组下标对应Device侧`GetHandleRef`的`channelIndex`。 |
| `channelNum` | 输入 | 通道描述数量，必须大于0。 |
| `multiChannel` | 输出 | Device侧多通道句柄。创建失败时置为0。 |

## 返回值

HCCL通信域重载成功时返回`HCCL_SUCCESS`，失败时返回对应的HCCL错误码。Hcomm Endpoint重载成功时返回成功码，失败时返回对应的Hcomm错误码。

## 约束说明

- 所有输入指针均不能为`nullptr`，`channelNum`必须大于0。
- 通信域重载要求`comm`已完成初始化。同一通信域内，每个`sharedQueueTag`只能用于一次成功调用。
- 通信域重载创建的资源由`comm`管理，并随通信域销毁自动释放，不能调用`DestroyMultiChannelHandle`释放。
- Endpoint重载要求`endpointHandle`已完成初始化并支持`COMM_ENGINE_AIV`和UB_CTP协议。
- Endpoint重载创建的资源由调用方管理。使用该句柄的Kernel执行并同步完成后，必须调用`DestroyMultiChannelHandle`，然后才能销毁`endpointHandle`。
- 创建的多通道资源不能被并发使用。
- `channelDescs`在Host侧的数组下标必须与Device侧传给`GetHandleRef`的`channelIndex`保持一致。

## 相关接口

- [DestroyMultiChannelHandle](DestroyMultiChannelHandle.md)
- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
