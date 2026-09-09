# FlushAsync

## 功能说明

获取指定peer对应的通信通道句柄，用于后续调用`Wait`等待该通道上的通信任务完成。

`FlushAsync`本身不阻塞等待通信任务完成，只根据`team`、`peer`和`Ain`实例的`contextIndex`解析通道句柄并写入`outChannelHandle`。

## 函数原型

```cpp
__aicore__ inline void FlushAsync(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::ChannelHandle* outChannelHandle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `outChannelHandle` | 输出 | 返回解析出的通信通道句柄。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建。
- `team`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `outChannelHandle`需要指向有效可写地址。
- `FlushAsync`不会等待任务完成，调用方需要继续调用`Wait`等待`outChannelHandle`对应通道上的任务完成。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
