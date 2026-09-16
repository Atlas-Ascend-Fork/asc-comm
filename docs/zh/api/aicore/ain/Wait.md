# Wait

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

等待指定通信通道上的通信任务完成。

通常先调用`FlushAsync`获取指定peer的通信通道句柄，再调用`Wait`等待该通道上的已提交任务完成。

## 函数原型

```cpp
__aicore__ inline void Wait(AscendC::ChannelHandle& channelHandle);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channelHandle` | 输入 | 通信通道句柄，通常由`FlushAsync`返回。 |

## 返回值说明

无返回值。

## 约束说明

- `channelHandle`需要是有效通信通道句柄。
- `Wait`通过底层Hcomm完成等待接口阻塞等待该通道上已提交的任务完成。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 调用示例

Wait接口的调用示例如下。

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
    // FlushAsync获取指定peer对应的通信通道句柄
    ain.FlushAsync(team, peer, &channelHandle);
    // 阻塞等待该通道上的通信任务完成
    ain.Wait(channelHandle);
}
```
