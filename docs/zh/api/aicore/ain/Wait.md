# Wait

## 功能说明

等待指定通信通道上的通信任务完成。

通常先调用`FlushAsync`获取指定peer的通信通道句柄，再调用`Wait`等待该通道上的已提交任务完成。

## 函数原型

```cpp
__aicore__ inline void Wait(AscendC::ChannelHandle& channelHandle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channelHandle` | 输入 | 通信通道句柄，通常由`FlushAsync`返回。 |

## 返回值

无返回值。

## 约束说明

- `channelHandle`需要是有效通信通道句柄。
- `Wait`通过底层Hcomm完成等待接口阻塞等待该通道上已提交的任务完成。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
