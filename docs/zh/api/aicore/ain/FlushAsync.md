# flush_async

## 功能说明

获取指定peer对应的通信通道句柄，用于后续调用`wait`等待该通道上的通信任务完成。

`flush_async`本身不阻塞等待通信任务完成，只根据`team`、`peer`和`ain`实例的`context_index`解析通道句柄并写入`out_channel_handle`。

## 函数原型

```cpp
__aicore__ inline void flush_async(
    AscendC::asccomm_team_handle team,
    uint32_t peer,
    AscendC::channel_handle* out_channel_handle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `out_channel_handle` | 输出 | 返回解析出的通信通道句柄。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建。
- `team`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `out_channel_handle`需要指向有效可写地址。
- `flush_async`不会等待任务完成，调用方需要继续调用`wait`等待`out_channel_handle`对应通道上的任务完成。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。
