# DestroyMultiChannelHandle

## 功能说明

销毁`MakeMultiChannelHandle`创建的共享Jetty通道和Device上下文。接口先调用`HcommChannelDestroy`销毁全部通道，再调用`HcommEngineCtxDestroy`释放多通道元数据。

## 函数原型

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `multiChannel` | 输入 | `MakeMultiChannelHandle`返回的有效句柄。 |

## 返回值

成功返回`HCCL_SUCCESS`，失败返回对应的Hcomm错误码。无论通道销毁是否成功，接口都会尝试释放Device上下文。

## 约束说明

- 调用前必须确保使用该句柄的Kernel已执行完成并同步。
- 每个句柄只能销毁一次，销毁后不能继续使用。
- 不能传入`MakeMultiChannelHandleWithComm`返回的通信域托管句柄。
- 应在`HcommEndpointDestroy`之前调用本接口。

## 相关接口

- [MakeMultiChannelHandle](MakeMultiChannelHandle.md)
