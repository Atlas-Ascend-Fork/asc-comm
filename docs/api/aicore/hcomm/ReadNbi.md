# ReadNbi

## 功能说明

通过指定通信通道提交点对点读任务，将数据从`src`读取到`dst`。

## 函数原型

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t ReadNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输出 | 目的GM地址。 |
| `src` | 输入 | 源GM地址。 |
| `len` | 输入 | 读取长度，单位为字节。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `commit` | 是否在提交任务时立即commit。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置，仅URMA路径使用。默认为`URMA_DEFAULT_CFG`（强序 + fence + 使能CQE）。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前通信通道需已完成初始化。
- `COMM_PROTOCOL_UBC_CTP`路径下，`src`需要落在通道注册的远端buffer范围内，`dst`为本端目标地址。

## 相关样例

参考[hcomm_write_read_nbi](../../../../examples/hcomm_write_read_nbi/README.md)中的AIV直驱URMA两卡读写流程。
