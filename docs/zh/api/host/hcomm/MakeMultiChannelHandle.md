# MakeMultiChannelHandle

## 功能说明

在Host侧创建一组共享Jetty的UBC_CTP通信通道，并创建Device侧使用的`MultiChannelHandle`。

接口按照`channelDescs`的顺序保存各逻辑通道的远端通信信息。返回的句柄可以传入AICore Kernel，并通过`MakeBatchHandle`创建多通道批量句柄。

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
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `comm` | 输入 | 已初始化的HCCL通信域。接口创建的共享Jetty通道和Device上下文由该通信域管理。 |
| `sharedQueueTag` | 输入 | 共享队列标签。同一通信域内使用该标签标识本组共享Jetty通道和对应的Device上下文。 |
| `channelDescs` | 输入 | 通道描述数组，包含`channelNum`个元素。数组顺序决定后续`GetHandleRef`使用的`channelIndex`。 |
| `channelNum` | 输入 | 通道描述数量，必须大于0。 |
| `multiChannel` | 输出 | Device侧多通道句柄。其底层值为通信域管理的Device上下文地址。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `HCCL_SUCCESS` | 创建成功，`multiChannel`返回有效句柄。 |
| 其他值 | 创建失败；当`multiChannel`非空时将其置为0，并返回对应的HCCL错误码。 |

## 约束说明

- 输入指针不能为`nullptr`，`channelNum`不能为0。
- `channelDescs`中的通道需要使用`COMM_ENGINE_AIV`和`COMM_PROTOCOL_UBC_CTP`，并满足HCCL共享队列通道约束。
- 同一通信域内，每个`sharedQueueTag`仅用于一次成功的`MakeMultiChannelHandle`调用，重复创建返回错误。
- 返回句柄及其Device上下文由`comm`管理，无需单独销毁。使用该句柄的Kernel完成前，不能销毁通信域。

## 相关接口

- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
