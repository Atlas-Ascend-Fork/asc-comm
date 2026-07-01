# WriteWithNotifyNbi

## 功能说明

提交点对点写任务，并在写任务中携带远端通知地址和通知值。

## 函数原型

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteWithNotifyNbi(
    AscendC::ChannelHandle channel,
    GM_ADDR dst,
    GM_ADDR src,
    uint64_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输出 | 目的 GM 地址。 |
| `src` | 输入 | 源 GM 地址。 |
| `len` | 输入 | 写入长度，单位为字节。 |
| `notifyAddr` | 输入 | 远端通知地址。 |
| `notifyVal` | 输入 | 远端通知值。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |
