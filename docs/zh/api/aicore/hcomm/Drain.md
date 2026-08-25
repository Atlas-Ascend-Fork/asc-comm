# Drain

## 功能说明

阻塞等待指定通道上的通信任务执行完成。

## 函数原型

```cpp
template <pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t Drain(AscendC::ChannelHandle channel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `pipe` | drain使用的pipe，默认`PIPE_MTE3`。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| 非`0` | 等待失败。`COMM_PROTOCOL_UBC_CTP`路径直接返回底层CQ轮询错误码：`0xFF`表示轮询超时，其他正值由CQE的status和substatus组合而成；其他协议可能返回`-1`。 |
