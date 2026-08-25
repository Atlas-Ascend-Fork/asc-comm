# MakeMultiChannelHandle

## 功能说明

基于Hcomm Endpoint创建一组共享Jetty的UB_CTP通信通道，并创建Device侧使用的`MultiChannelHandle`。接口内部调用`HcommChannelCreateWithConfig`并等待所有通道建链就绪，然后按照`channelDescs`顺序保存各逻辑通道的远端通信信息。

头文件为：

```cpp
#include "hcomm/hcomm_host.h"
```

## 函数原型

```cpp
HcommResult MakeMultiChannelHandle(
    EndpointHandle endpointHandle,
    HcommChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `endpointHandle` | 输入 | 已初始化的Hcomm Endpoint。该Endpoint必须支持AIV引擎的UB_CTP协议。 |
| `channelDescs` | 输入 | 包含`channelNum`个元素的Hcomm通道描述数组。数组顺序决定`GetHandleRef`使用的`channelIndex`。 |
| `channelNum` | 输入 | 通道描述数量，必须大于0。 |
| `multiChannel` | 输出 | Device侧多通道句柄。创建失败时置为0。 |

## 返回值

成功返回`HCCL_SUCCESS`，失败返回对应的Hcomm错误码。建链失败、资源不足或120秒内未全部就绪时，接口销毁已创建的通道并返回错误。

## 约束说明

- 输入指针不能为`nullptr`，`channelNum`不能为0。
- 通道使用`COMM_ENGINE_AIV`和UB_CTP协议，并遵循`HcommChannelCreateWithConfig`的共享Jetty约束。
- 同一Endpoint下具有相同源端Endpoint Pair的通道共享Jetty。共享Jetty通道不能并发使用。
- 返回句柄拥有新建的通道和Device上下文。Kernel使用完成并同步后，必须调用`DestroyMultiChannelHandle`，之后才能销毁`endpointHandle`。
- 通信域路径请使用`MakeMultiChannelHandleWithComm`。

## 相关接口

- [DestroyMultiChannelHandle](DestroyMultiChannelHandle.md)
- [MakeMultiChannelHandleWithComm](MakeMultiChannelHandleWithComm.md)
- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
