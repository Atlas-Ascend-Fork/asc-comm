# DestroyMultiChannelHandleWithComm

## 功能说明

释放`MakeMultiChannelHandleWithComm`创建的Device侧多通道上下文。共享Jetty通道继续由HCCL通信域统一管理。

头文件为：

```cpp
#include "hcomm/hcomm_host.h"
```

## 函数原型

```cpp
HcclResult DestroyMultiChannelHandleWithComm(HcclComm comm, const char* sharedQueueTag);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `comm` | 输入 | 创建多通道句柄时使用的HCCL通信域。 |
| `sharedQueueTag` | 输入 | 创建多通道句柄时使用的共享队列标签。 |

## 返回值

成功返回`HCCL_SUCCESS`，失败返回对应的HCCL错误码。

## 约束说明

- 调用前必须确保使用该多通道句柄的Kernel已经执行并同步完成，调用后对应的`MultiChannelHandle`失效。
- 接口通过`HcclEngineCtxDestroy`释放Device上下文，不销毁共享通道；共享通道随`comm`统一释放。
- 只能销毁`MakeMultiChannelHandleWithComm`创建的上下文。Endpoint路径使用`DestroyMultiChannelHandle`。

## 相关接口

- [MakeMultiChannelHandleWithComm](MakeMultiChannelHandleWithComm.md)
- [DestroyMultiChannelHandle](DestroyMultiChannelHandle.md)
