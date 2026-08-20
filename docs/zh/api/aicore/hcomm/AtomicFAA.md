# atomic_faa

## 功能说明

提交Fetch-and-add原子操作任务，对远端地址`dst`中的值执行原子加，并将加法前的旧值写入本端`fetch_addr`。

当前该接口仅支持`comm_protocol_ubc_ctp`路径。

## 函数原型

```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commit_pipe = PIPE_S,
    pipe_t req_pipe = PIPE_MTE3,
    auto const& config = urma_default_cfg>
__aicore__ inline int32_t atomic_faa(
    AscendC::channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输入/输出 | 远端原子操作目标GM地址。 |
| `fetch_addr` | 输出 | 本端GM地址，用于保存远端地址执行原子加前的旧值。 |
| `add_val` | 输入 | 加到远端目标地址的值。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 原子操作数据类型，仅支持`int32_t`、`uint32_t`、`int64_t`、`uint64_t`。 |
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
- `dst`需要落在通道注册的远端buffer范围内，`fetch_addr`用于保存旧值，长度为`sizeof(T)`。
- 单个`atomic_faa`任务在URMA SQ中占用2个WQE block。
