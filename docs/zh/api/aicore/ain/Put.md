# Put

## 功能说明

通过指定通信team和对端rank，提交单边写任务，将本端对称window中的数据写入对端对称window。

`Put`会根据`team`、`peer`和`Ain`实例的`contextIndex`解析通信通道，根据`dstWin`、`srcWin`及offset解析远端/本端GM地址，并通过底层Hcomm写接口提交通信任务。可选配置`AinSignalInc`或`AinSignalAdd`作为远端动作，在写任务后追加一次远端signal原子加操作。

## 函数原型

```cpp
template <
    typename RemoteAction = AscendC::AinRemoteNone,
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_DEFAULT_CFG>
__aicore__ inline void Put(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::HcommWindowHandle dstWin,
    uint64_t dstOffset,
    AscendC::HcommWindowHandle srcWin,
    uint64_t srcOffset,
    uint64_t bytes,
    RemoteAction remoteAction = RemoteAction{},
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `dstWin` | 输入 | 对端目的对称window句柄。 |
| `dstOffset` | 输入 | 目的对称window内的字节偏移。 |
| `srcWin` | 输入 | 本端源对称window句柄。 |
| `srcOffset` | 输入 | 源对称window内的字节偏移。 |
| `bytes` | 输入 | 写入长度，单位为字节。 |
| `remoteAction` | 输入 | 写任务后的远端动作，默认`AinRemoteNone`，不触发远端signal操作。 |
| `ubuf` | 输入 | 底层Hcomm使用的UBuf临时工作区描述。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `RemoteAction` | 远端动作类型，支持`AinRemoteNone`、`AinSignalInc`和`AinSignalAdd`。 |
| `DescriptorUbuf` | UBuf临时工作区描述类型，默认`AinDescriptorUbuf`。 |
| `CommitFlags` | 任务提交方式。`AIN_COMMIT_IMMED`表示组装WQE后立即响铃提交；`AIN_COMMIT_DELAYED`表示只组装WQE，需后续调用一个`AIN_COMMIT_IMMED`任务统一触发提交。 |
| `Config` | URMA WQE控制配置，默认`URMA_DEFAULT_CFG`(强保序/FenceOn/CqeOn)。 |

## 远端动作

| 类型 | 说明 |
| --- | --- |
| `AinRemoteNone` | 不追加远端动作。 |
| `AinSignalInc` | 写任务后对`remoteAction.signalWindow`中`remoteAction.signalOffset`对应的远端signal执行原子加1。 |
| `AinSignalAdd` | 写任务后对`remoteAction.signalWindow`中`remoteAction.signalOffset`对应的远端signal执行原子加`remoteAction.value`。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建及对称window注册。
- `team`、`dstWin`和`srcWin`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `dstOffset + bytes`需要落在`peer`对应的`dstWin`注册内存范围内，`srcOffset + bytes`需要落在本rank对应的`srcWin`注册内存范围内。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层Hcomm初始化使用的UBuf临时工作区；`DescriptorUbuf`当前必须提供不小于512B的UBuf临时工作区。
- 使用`AIN_COMMIT_DELAYED`时，本次任务不会立即响铃提交，需要后续至少提交一次`AIN_COMMIT_IMMED`的任务来保证之前任务已被提交。
- `Put`提交的是非阻塞通信任务；需要调用`Flush`等待team内所有peer通道任务完成，或通过`FlushAsync`获取指定peer通道后调用`Wait`等待完成。
- `AinSignalInc`和`AinSignalAdd`通过底层Hcomm原子加实现，signal地址应按`uint64_t`访问要求准备。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径，由于底层协议限制，单次调用`Put`数据传输长度最大不超过256MB，即入参bytes需要小于等于256 * 1024 * 1024。
