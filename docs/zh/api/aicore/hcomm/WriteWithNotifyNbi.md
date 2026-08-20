# write_with_notify_nbi

## 功能说明

提交点对点写任务，并在写任务中携带远端通知地址和通知值。

当前该接口仅支持`comm_protocol_ubc_ctp`路径。`comm_protocol_roce`路径保留同名接口，但实现会返回失败。

## 函数原型

```cpp
template <
    bool commit = true,
    pipe_t commit_pipe = PIPE_S,
    pipe_t req_pipe = PIPE_MTE3,
    auto const& config = urma_default_cfg>
__aicore__ inline int32_t write_with_notify_nbi(
    AscendC::channel_handle channel,
    GM_ADDR dst,
    GM_ADDR src,
    uint64_t len,
    GM_ADDR notify_addr,
    uint64_t notify_val);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输出 | 目的GM地址。 |
| `src` | 输入 | 源GM地址。 |
| `len` | 输入 | 写入长度，单位为字节。 |
| `notify_addr` | 输入 | 远端通知地址。 |
| `notify_val` | 输入 | 远端通知值。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `commit` | 是否在提交任务时立即commit。 |
| `commit_pipe` | commit使用的pipe，默认`PIPE_S`。 |
| `req_pipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置，仅URMA路径使用。默认为`urma_default_cfg`（强序 + fence + 使能CQE）。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前通信通道需已完成初始化。
- 传入的`channel_handle`需要对应`comm_protocol_ubc_ctp`通道。
- `comm_protocol_roce`路径不支持该接口，调用会返回`-1`。
- 单个`write_with_notify_nbi`任务在URMA SQ中占用2个WQE block。
