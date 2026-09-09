# Signal

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->

## 功能说明

通过指定通信组team和对端rank，对远端signal执行原子加操作。

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

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `RemoteAction` | signal动作类型，支持`AinSignalInc`和`AinSignalAdd`，见表3。 |
| `DescriptorUbuf` | UBuf临时工作区描述类型，默认`AinDescriptorUbuf`。 |
| `CommitFlags` | 任务提交方式。`AIN_COMMIT_IMMED`表示组装WQE后立即响铃提交；`AIN_COMMIT_DELAYED`表示只组装WQE，需后续调用一个`AIN_COMMIT_IMMED`任务统一触发提交。 |
| `Config` | URMA WQE控制配置，默认`URMA_DEFAULT_CFG`(AI Core为relax order + CQE保序上报 + 启用fence + 需要上报CQE)。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `team` | 输入 | 通信组team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `action` | 输入 | signal动作，支持`AinSignalInc`和`AinSignalAdd`。 |
| `ubuf` | 输入 | 底层Hcomm使用的UBuf临时工作区描述。 |

**表3** Signal动作说明

| Signal动作 | 描述 |
| --- | --- |
| `AinSignalInc{signalWindow, signalOffset}` | 对远端signal执行原子加1。 |
| `AinSignalAdd{signalWindow, signalOffset, value}` | 对远端signal执行原子加`value`。 |

其中，`signalWindow`为目的signal所在的对称window句柄，`signalOffset`为signal在`signalWindow`内的字节偏移，`value`为原子加的值。

## 返回值说明

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
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 调用示例

Signal接口的调用示例如下。

```cpp
// 初始化UBuf工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* signalWin,
    uint32_t rankNum, uint32_t rankId)
{
    // 准备UBuf工作区
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        HCOMM_BUF_SIZE, 0U};
    // 创建Ain对象
    AscendC::Ain ain;
    // 对端rankId
    uint32_t peer = (rankId + 1) % rankNum;
    // signal原子加的值
    uint64_t value = 1U;

    // 对远端signal执行原子递增操作，每次调用加1
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);

    // 对远端signal执行原子加value操作
    ain.Signal<AscendC::AinSignalAdd>(team, peer,
        AscendC::AinSignalAdd{signalWin, 0U, value}, hcommUbuf);

    // 等待team内所有peer通道通信任务完成
    ain.Flush(team);
}
```
