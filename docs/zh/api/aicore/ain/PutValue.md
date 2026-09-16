# PutValue

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

通过指定通信组team和对端rank，提交点对点立即数写任务，将`value`写入对端对称window。

`PutValue`会根据`team`、`peer`和`Ain`实例的`contextIndex`解析通信通道，根据`dstWin`及`dstOffset`解析远端GM地址，并通过底层Hcomm立即数写接口提交通信任务。可选配置`AinSignalInc`或`AinSignalAdd`作为远端动作，在写任务后追加一次远端signal原子加操作。

## 函数原型

```cpp
template <
    typename T,
    typename RemoteAction = AscendC::AinRemoteNone,
    typename DescriptorUbuf = AscendC::AinDescriptorUbuf,
    AscendC::AinCommitFlags CommitFlags = AscendC::AIN_COMMIT_IMMED,
    auto const& Config = URMA_INLINE_CFG>
__aicore__ inline void PutValue(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::HcclCommSymWindow dstWin,
    uint64_t dstOffset,
    T value,
    RemoteAction remoteAction = RemoteAction{},
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 立即数类型。 |
| `RemoteAction` | 远端动作类型，支持`AinRemoteNone`、`AinSignalInc`和`AinSignalAdd`，见表3。 |
| `DescriptorUbuf` | UBuf临时工作区描述类型，默认`AinDescriptorUbuf`。 |
| `CommitFlags` | 任务提交方式。`AIN_COMMIT_IMMED`表示组装WQE后立即响铃提交；`AIN_COMMIT_DELAYED`表示只组装WQE，需后续调用一个`AIN_COMMIT_IMMED`任务统一触发提交。 |
| `Config` | URMA WQE控制配置，默认`URMA_INLINE_CFG`(执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE + 携带inline数据)。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `team` | 输入 | 通信组team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `dstWin` | 输入 | 对端目的对称window句柄。 |
| `dstOffset` | 输入 | 目的对称window内的字节偏移。 |
| `value` | 输入 | 需要写入对端的立即数。 |
| `remoteAction` | 输入 | 写任务后的远端动作，默认`AinRemoteNone`，不追加远端动作。 |
| `ubuf` | 输入 | 底层Hcomm使用的UBuf临时工作区描述。 |

**表3** 远端动作说明

| 远端动作 | 描述 |
| --- | --- |
| `AinRemoteNone{}` | 不追加远端动作。 |
| `AinSignalInc{signalWindow, signalOffset}` | 写任务后对远端signal执行原子加1。 |
| `AinSignalAdd{signalWindow, signalOffset, value}` | 写任务后对远端signal执行原子加`value`。 |

其中，`signalWindow`为目的signal所在的对称window句柄，`signalOffset`为signal在`signalWindow`内的字节偏移，`value`为原子加的值。

## 返回值说明

无返回值。

## 约束说明

- 调用前Host侧需完成team创建及对称window注册。
- `team`和`dstWin`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `dstOffset + sizeof(T)`需要落在`peer`对应的`dstWin`注册内存范围内。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层Hcomm初始化使用的UBuf临时工作区；`DescriptorUbuf`当前必须提供不小于512B的UBuf临时工作区。
- 使用`AIN_COMMIT_DELAYED`时，本次任务不会立即响铃提交，需要后续至少提交一次`AIN_COMMIT_IMMED`的任务来保证之前任务已被提交。
- 使用`AIN_COMMIT_DELAYED`时，连续调用次数不得超过底层SQ深度（`sqDepth`），需在SQ耗尽前通过`AIN_COMMIT_IMMED`提交积攒的任务，否则后续任务将因SQ溢出而失败。
- 批量提交场景（多次`AIN_COMMIT_DELAYED` + 最后一次`AIN_COMMIT_IMMED`）下，仅最后一次提交应产生CQE。
- `PutValue`提交的是非阻塞通信任务；需要调用`Flush`等待team内所有peer通道任务完成，或通过`FlushAsync`获取指定peer通道后调用`Wait`等待完成。
- `AinSignalInc`和`AinSignalAdd`通过底层Hcomm原子加实现，signal地址应按`uint64_t`访问要求准备。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 调用示例

PutValue接口的调用示例如下。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = sizeof(uint32_t);
// 初始化UBuf工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* dstWin, __gm__ void* signalWin,
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
    // 待写入对端的立即数
    uint32_t value = 42U;
    // 立即数写：将value写入对端dstWin，写完成后对远端signal原子加1
    ain.PutValue(team, peer, dstWin, rankId * BYTES, value,
                 AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);
    // 等待team内所有peer通道通信任务完成
    ain.Flush(team);
}
```
