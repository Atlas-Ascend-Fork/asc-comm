# get

## 功能说明

通过指定通信team和对端rank，提交单边读任务，将对端对称window中的数据读取到本端对称window。

`get`会根据`team`、`peer`和`ain`实例的`context_index`解析通信通道，根据`dst_win`、`src_win`及offset解析本端/远端GM地址，并通过底层asccomm读接口提交通信任务。

## 函数原型

```cpp
template <
    typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf,
    AscendC::ain_commit_flags commit_flags = AscendC::ain_commit_immed,
    auto const& config = urma_default_cfg>
__aicore__ inline void get(
    AscendC::asccomm_team_handle team,
    uint32_t peer,
    AscendC::asccomm_window_handle dst_win,
    uint64_t dst_offset,
    AscendC::asccomm_window_handle src_win,
    uint64_t src_offset,
    uint64_t bytes,
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `dst_win` | 输入 | 本端目的对称window句柄。 |
| `dst_offset` | 输入 | 目的对称window内的字节偏移。 |
| `src_win` | 输入 | 对端源对称window句柄。 |
| `src_offset` | 输入 | 源对称window内的字节偏移。 |
| `bytes` | 输入 | 读取长度，单位为字节。 |
| `ubuf` | 输入 | 底层asccomm使用的UBuf临时工作区描述。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `descriptor_ubuf_t` | UBuf临时工作区描述类型，默认`ain_descriptor_ubuf`。 |
| `commit_flags` | 任务提交方式。`ain_commit_immed`表示组装WQE后立即响铃提交；`ain_commit_delayed`表示只组装WQE，需后续调用一个`ain_commit_immed`任务统一触发提交。 |
| `config` | URMA WQE控制配置，默认`urma_default_cfg`(强保序/FenceOn/CqeOn)。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建及对称window注册。
- `team`、`dst_win`和`src_win`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `dst_offset + bytes`需要落在本rank对应的`dst_win`注册内存范围内，`src_offset + bytes`需要落在`peer`对应的`src_win`注册内存范围内。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层asccomm初始化使用的UBuf临时工作区；`descriptor_ubuf_t`当前必须提供不小于512B的UBuf临时工作区。
- 使用`ain_commit_delayed`时，本次任务不会立即响铃提交，需要后续至少提交一次`ain_commit_immed`的任务来保证之前任务已被提交。
- `get`提交的是非阻塞通信任务；需要调用`flush`等待team内所有peer通道任务完成，或通过`flush_async`获取指定peer通道后调用`wait`等待完成。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。单次调用`get`的入参`bytes`取值范围为`0 < bytes <= 256 * 1024 * 1024`。
