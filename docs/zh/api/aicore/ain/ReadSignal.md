# ReadSignal

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

读取本rank本地signal值，并按`bits`指定的低位宽度返回掩码后的结果。

`ReadSignal`会根据`team`、`signalWindow`和`signalOffset`解析本rank对应的signal地址，并通过device侧读操作读取`uint64_t` signal值。

## 函数原型

```cpp
__aicore__ inline uint64_t ReadSignal(
    AscendC::HcommTeamHandle team,
    AscendC::HcclCommSymWindow signalWindow,
    size_t signalOffset,
    uint32_t bits = 64,
    AscendC::AinMemoryOrder order = AscendC::AIN_MEMORY_ORDER_RELAX) const;
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `team` | 输入 | 通信组team句柄。 |
| `signalWindow` | 输入 | signal所在对称window句柄。 |
| `signalOffset` | 输入 | signal在对称window内的字节偏移。 |
| `bits` | 输入 | 读取低位bit数，取值范围为1-64，默认64。 |
| `order` | 输入 | signal读内存序，默认`AIN_MEMORY_ORDER_RELAX`。 |

## 返回值说明

| 返回值 | 描述 |
| --- | --- |
| `uint64_t` | 本rank本地signal低`bits`位掩码后的值。 |

## 约束说明

- 调用前Host侧需完成team创建及signal所在对称window注册。
- `team`和`signalWindow`需要是有效的device侧句柄。
- `signalOffset`对应地址需要落在本rank对应的signal window注册内存范围内。
- signal地址应按`uint64_t`访问要求准备。
- `bits`用于生成低位掩码；取值范围为1-64，当`bits`为64时读取完整64位signal值。
- 当前仅支持`AIN_MEMORY_ORDER_RELAX`。该内存序只保证signal原子读写及阈值检查，不保证signal操作前后普通数据访问的内存序。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 调用示例

ReadSignal接口的调用示例如下。

```cpp
// 初始化UBuf工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* signalWin, uint32_t rankNum, uint32_t rankId)
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
    // 对远端signal执行原子递增操作，每次调用加1
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);
    // 期望的signal阈值
    uint64_t expectVal = 1U;
    // 阻塞等待signal信号
    while(true) {
        // 读取本端signal值
        uint64_t signalVal = ain.ReadSignal(team, signalWin, 0U);
        // 阻塞直至收到远端发来的signal信号：signalVal >= expectVal
        if (signalVal >= expectVal) {
            break;
        }
    }
}
```
