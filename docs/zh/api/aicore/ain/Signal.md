# signal

## 功能说明

通过指定通信team和对端rank，对远端signal执行原子加操作。

`signal`会根据`team`、`peer`和`ain`实例的`context_index`解析通信通道，根据`action.signal_window`及`action.signal_offset`解析远端signal地址，并通过底层asccomm提交原子加任务。

## 函数原型

```cpp
template <
    typename remote_action_t,
    typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf,
    AscendC::ain_commit_flags commit_flags = AscendC::ain_commit_immed,
    auto const& config = urma_default_cfg>
__aicore__ inline void signal(
    AscendC::asccomm_team_handle team,
    uint32_t peer,
    remote_action_t action,
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `action` | 输入 | signal动作，支持`ain_signal_inc`和`ain_signal_add`。 |
| `ubuf` | 输入 | 底层asccomm使用的UBuf临时工作区描述。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `remote_action_t` | signal动作类型，支持`ain_signal_inc`和`ain_signal_add`。 |
| `descriptor_ubuf_t` | UBuf临时工作区描述类型，默认`ain_descriptor_ubuf`。 |
| `commit_flags` | 任务提交方式。`ain_commit_immed`表示组装WQE后立即响铃提交；`ain_commit_delayed`表示只组装WQE，需后续调用一个`ain_commit_immed`任务统一触发提交。 |
| `config` | URMA WQE控制配置，默认`urma_default_cfg`(强保序/FenceOn/CqeOn)。 |

## signal动作

| 类型 | 说明 |
| --- | --- |
| `ain_signal_inc` | 对`action.signal_window`中`action.signal_offset`对应的远端signal执行原子加1。 |
| `ain_signal_add` | 对`action.signal_window`中`action.signal_offset`对应的远端signal执行原子加`action.value`。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建及signal所在对称window注册。
- `team`和`action.signal_window`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `action.signal_offset`对应地址需要落在`peer`对应的signal window注册内存范围内。
- signal地址应按`uint64_t`访问要求准备。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层asccomm初始化使用的UBuf临时工作区；`descriptor_ubuf_t`当前必须提供不小于512B的UBuf临时工作区。
- 使用`ain_commit_delayed`时，本次任务不会立即响铃提交，需要后续至少提交一次`ain_commit_immed`的任务来保证之前任务已被提交。
- `signal`提交的是非阻塞通信任务；需要调用`flush`等待team内所有peer通道任务完成，或通过`flush_async`获取指定peer通道后调用`wait`等待完成。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。
