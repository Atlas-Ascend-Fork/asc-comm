# Signal

## 功能说明

通过指定通信team和对端rank，对远端signal执行原子加操作。

`Signal`会根据`team`、`peer`和`Ain`实例的`contextIndex`解析通信通道，根据`action.signalWindow`及`action.signalOffset`解析远端signal地址，并通过底层Hcomm提交原子加任务。

## 函数原型

```cpp
template <
    typename RemoteAction,
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_DEFAULT_CFG>
__aicore__ inline void Signal(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    RemoteAction action,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `action` | 输入 | signal动作，支持`AinSignalInc`和`AinSignalAdd`。 |
| `ubuf` | 输入 | 底层Hcomm使用的UBuf临时工作区描述。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `RemoteAction` | signal动作类型，支持`AinSignalInc`和`AinSignalAdd`。 |
| `DescriptorUbuf` | UBuf临时工作区描述类型，默认`AinDescriptorUbuf`。 |
| `CommitFlags` | 任务提交方式。`AIN_COMMIT_IMMED`表示组装WQE后立即响铃提交；`AIN_COMMIT_DELAYED`表示只组装WQE，需后续调用一个`AIN_COMMIT_IMMED`任务统一触发提交。 |
| `Config` | URMA WQE控制配置，默认`URMA_DEFAULT_CFG`(强保序/FenceOn/CqeOn)。 |

## Signal动作

| 类型 | 说明 |
| --- | --- |
| `AinSignalInc` | 对`action.signalWindow`中`action.signalOffset`对应的远端signal执行原子加1。 |
| `AinSignalAdd` | 对`action.signalWindow`中`action.signalOffset`对应的远端signal执行原子加`action.value`。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建及signal所在对称window注册。
- `team`和`action.signalWindow`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `action.signalOffset`对应地址需要落在`peer`对应的signal window注册内存范围内。
- signal地址应按`uint64_t`访问要求准备。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层Hcomm初始化使用的UBuf临时工作区；`DescriptorUbuf`当前必须提供不小于512B的UBuf临时工作区。
- 使用`AIN_COMMIT_DELAYED`时，本次任务不会立即响铃提交，需要后续至少提交一次`AIN_COMMIT_IMMED`的任务来保证之前任务已被提交。
- 使用`AIN_COMMIT_DELAYED`时，连续调用次数不得超过底层SQ深度（`sqDepth`），需在SQ耗尽前通过`AIN_COMMIT_IMMED`提交积攒的任务，否则后续任务将因SQ溢出而失败。
- 批量提交场景（多次`AIN_COMMIT_DELAYED` + 最后一次`AIN_COMMIT_IMMED`）下，仅最后一次提交应产生CQE。
- `Signal`提交的是非阻塞通信任务；需要调用`Flush`等待team内所有peer通道任务完成，或通过`FlushAsync`获取指定peer通道后调用`Wait`等待完成。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
