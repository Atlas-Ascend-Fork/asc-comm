# FlushAsync

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

获取指定peer对应的通信通道句柄，用于后续调用`Wait`等待该通道上的通信任务完成。

`FlushAsync`本身不阻塞等待通信任务完成，只根据`team`、`peer`和`Ain`实例的`contextIndex`解析通道句柄并写入`outChannelHandle`。

## 函数原型

```cpp
__aicore__ inline void FlushAsync(
    AscendC::HcommTeamHandle team,
    uint32_t peer,
    AscendC::ChannelHandle* outChannelHandle);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `team` | 输入 | 通信组team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `outChannelHandle` | 输出 | 返回解析出的通信通道句柄。 |

## 返回值说明

无返回值。

## 约束说明

- 调用前Host侧需完成team创建。
- `team`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `outChannelHandle`需要指向有效可写地址。
- `FlushAsync`不会等待任务完成，调用方需要继续调用`Wait`等待`outChannelHandle`对应通道上的任务完成。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 调用示例

FlushAsync接口的调用示例如下。

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
    // 提交单边写任务
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    AscendC::ChannelHandle channelHandle{0U};
    // 获取指定peer对应的通信通道句柄
    ain.FlushAsync(team, peer, &channelHandle);
    // 等待该通道上的通信任务完成
    ain.Wait(channelHandle);
}
```
