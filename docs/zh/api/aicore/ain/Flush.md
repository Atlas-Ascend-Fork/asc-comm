# Flush

## 功能说明

等待指定通信team内所有peer通道上的通信任务完成。

`Flush`会遍历team内除本rank外的所有rank，根据`Ain`实例的`contextIndex`解析对应通信通道，并对每个通道调用底层Hcomm完成等待接口。

## 函数原型

```cpp
__aicore__ inline void Flush(AscendC::HcommTeamHandle team);
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
- `Flush`会等待team内所有peer通道上的已提交任务完成；如只需要等待单个peer通道，可通过`FlushAsync`获取通道句柄后调用`Wait`。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
