# flush

## 功能说明

等待指定通信team内所有peer通道上的通信任务完成。

`flush`会遍历team内除本rank外的所有rank，根据`ain`实例的`context_index`解析对应通信通道，并对每个通道调用底层asccomm完成等待接口。

## 函数原型

```cpp
__aicore__ inline void flush(AscendC::asccomm_team_handle team);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建。
- `team`需要是有效的device侧句柄。
- `flush`会等待team内所有peer通道上的已提交任务完成；如只需要等待单个peer通道，可通过`flush_async`获取通道句柄后调用`wait`。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。
