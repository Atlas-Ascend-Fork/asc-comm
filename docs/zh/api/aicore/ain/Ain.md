# Ain

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

`AscendC::Ain`是AI Core侧单边通信接口模板，提供`Put`/`Get`/`Signal`/`Flush`等单边通信及同步能力。`Ain`会根据通信组team、对端rank和对称window句柄解析通信通道及通信内存地址，Kernel侧无需直接维护远端地址。

## 函数原型

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class Ain;
```

构造函数：

```cpp
__aicore__ inline Ain(uint32_t contextIndex = 0);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `CommEngineMask` | 通信引擎选择掩码，默认值为`AIN_MASK_DEFAULT`，对应通信引擎为AIV。目前仅支持AIV，暂不支持其他通信引擎。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `contextIndex` | 输入 | 通信上下文索引，默认值为0。 |

## 约束说明

- 调用前Host侧需完成team创建、对称window注册。
- 传入的`HcommTeamHandle`和`HcclCommSymWindow`需要是有效的device侧句柄。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。
- `Put`、`PutValue`、`Get`和`Signal`内部会初始化底层Hcomm，调用时需要提供有效的`AinDescriptorUbuf`。
- 使用`AIN_COMMIT_DELAYED`时，连续调用次数不得超过底层SQ深度（`sqDepth`），需在SQ耗尽前通过`AIN_COMMIT_IMMED`提交积攒的任务，否则后续任务将因SQ溢出而失败。
- 批量提交场景（多次`AIN_COMMIT_DELAYED` + 最后一次`AIN_COMMIT_IMMED`）下，仅最后一次提交应产生CQE。

## 调用示例

Ain接口的调用示例如下。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UBuf工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin,
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
    // 单边写：本端srcWin数据写入对端dstWin
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // 等待team内所有peer通道通信任务完成
    ain.Flush(team);
}
```
