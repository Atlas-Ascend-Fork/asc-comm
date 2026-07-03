# Commit

## 功能说明

通知指定通道上已提交的通信任务可以开始执行。通常在调用`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`或`AtomicCAS`时将`commit`设置为`false`后显式调用。

## 函数原型

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `pipe` | commit使用的pipe，默认`PIPE_S`。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |
