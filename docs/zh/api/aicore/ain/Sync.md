# Sync

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

执行`AinBarrierSession`绑定team内的barrier同步。

`Sync`会先向team内其他rank发送barrier signal，再等待其他rank写入本rank的barrier signal。无超时重载会一直等待直到同步完成；带超时重载在轮询次数超过`timeoutCycles`后返回失败。

## 函数原型

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline void Sync(
    AscendC::AinMemoryOrder order,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline int32_t Sync(
    AscendC::AinMemoryOrder order,
    uint64_t timeoutCycles,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `DescriptorUbuf` | UBuf临时工作区描述类型，默认`AinDescriptorUbuf`。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `order` | 输入 | barrier signal等待内存序，当前支持`AIN_MEMORY_ORDER_RELAX`。 |
| `timeoutCycles` | 输入 | 最大轮询次数，仅带超时重载使用。 |
| `ubuf` | 输入 | 底层Hcomm使用的UBuf临时工作区描述。 |

## 返回值说明

无超时重载无返回值。

带超时重载返回值如下：

| 返回值 | 描述 |
| --- | --- |
| `0` | 同步成功。 |
| `-1` | 等待超过`timeoutCycles`，同步失败。 |

## 约束说明

- 调用前需要先构造有效的`AinBarrierSession`。
- 调用前Host侧需完成team创建，并准备barrier使用的同步内存。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层Hcomm初始化使用的UBuf临时工作区；`DescriptorUbuf`当前必须提供不小于512B的UBuf临时工作区。
- 当前仅支持`AIN_MEMORY_ORDER_RELAX`。该内存序只保证signal原子读写及阈值检查，不保证signal操作前后普通数据访问的内存序。
- 无超时重载会持续轮询直到所有其他rank到达barrier，调用方需要保证team内rank对称调用，避免Kernel永久等待。
- 带超时重载的`timeoutCycles`表示轮询次数，返回`-1`时只表示本rank等待超时，不会回滚已经发出的signal。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 调用示例

Sync接口的调用示例如下。

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
    // barrier资源索引
    uint32_t index = 0U;
    // 创建Ain对象
    AscendC::Ain ain(index);
    AscendC::AinBarrierSession barrierSession(&ain, team, index);
    // 带超时barrier同步
    int32_t ret = barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 10000U, hcommUbuf);
    if (ret != 0) {
        return;
    }
    // 对端rankId
    uint32_t peer = (rankId + 1) % rankNum;
    // 单边写：本端srcWin数据写入对端dstWin
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // 无超时barrier同步
    barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
}
```
