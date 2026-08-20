# wait

## 功能说明

等待指定通信通道上的通信任务完成。

通常先调用`flush_async`获取指定peer的通信通道句柄，再调用`wait`等待该通道上的已提交任务完成。

## 函数原型

```cpp
__aicore__ inline void wait(AscendC::channel_handle& channel_handle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel_handle` | 输入 | 通信通道句柄，通常由`flush_async`返回。 |

## 返回值

无返回值。

## 约束说明

- `channel_handle`需要是有效通信通道句柄。
- `wait`通过底层asccomm完成等待接口阻塞等待该通道上已提交的任务完成。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。
