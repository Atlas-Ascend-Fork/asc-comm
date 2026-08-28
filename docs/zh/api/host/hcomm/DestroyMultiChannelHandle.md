# DestroyMultiChannelHandle

## 功能说明

销毁`MakeMultiChannelHandle`的Endpoint重载创建的共享Jetty通道和Device上下文。接口先调用`HcommChannelDestroy`销毁全部通道，再调用`aclrtFree`释放多通道元数据。

头文件为：

```cpp
#include "hcomm/hcomm_host.h"
```

## 函数原型

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `multiChannel` | 输入 | Endpoint重载的`MakeMultiChannelHandle`返回的有效句柄。 |

## 返回值

成功返回`HCCL_SUCCESS`，失败返回对应的Hcomm错误码。无论通道销毁是否成功，接口都会尝试释放Device上下文。

## 约束说明

- 调用前必须确保使用该句柄的Kernel已执行完成并同步。
- 本接口只能用于Endpoint重载创建的句柄，并且应在`HcommEndpointDestroy`之前调用。通信域重载创建的资源由通信域自动释放，不能调用本接口。
- 每个多通道句柄只能销毁一次，销毁后不能继续使用。

## 相关接口

- [MakeMultiChannelHandle](MakeMultiChannelHandle.md)
