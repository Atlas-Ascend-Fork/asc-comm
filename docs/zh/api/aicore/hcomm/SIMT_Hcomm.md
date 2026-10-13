# Hcomm

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

`AscendC::simt::Hcomm`是SIMT Kernel侧点对点通信接口模板。普通接口通过`channel`逐条提交读、写、写通知和原子操作任务；批量接口通过[MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄添加任务，再通过[BatchCommit](./SIMT_BatchCommit.md)统一提交；完成集接口供SIMT线程轮询通信任务完成状态。批量任务还支持通过Group协作组由组内多个线程协作提交。

头文件为：

```cpp
#include "hcomm/hcomm_simt.h"
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commProtocol` | 通信协议类型，当前仅支持`COMM_PROTOCOL_UB_CTP`，默认值为`COMM_PROTOCOL_UB_CTP`，选定协议即确定对应通信路径。 |

## 约束说明

- SIMT实现当前仅支持`COMM_PROTOCOL_UB_CTP`协议路径，不支持`COMM_PROTOCOL_ROCE`。
- 调用通信接口前，通信通道资源需要由调用方完成初始化。
- `commit`模板参数设为`false`时任务延迟提交，后续需要由`commit`为`true`的调用统一触发提交；批量提交场景下，仅最后一次commit应生成完成记录。
- SIMT接口的完成等待除[Drain](./SIMT_Drain.md)外，还可通过完成集接口（[InitCompletionSet](./SIMT_InitCompletionSet.md)、[PrepareCompletion](./SIMT_PrepareCompletion.md)、[Progress](./SIMT_Progress.md)、[WaitCompletion](./SIMT_WaitCompletion.md)）轮询完成状态。

## 调用示例

Hcomm接口的调用示例如下。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;

__simt_callee__ void SimtWriteOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBuffer)
{
    // 创建SIMT Hcomm对象并初始化，SIMT URMA路径无需临时工作区
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 本端localBuffer数据写入远端remoteBuffer
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // 阻塞等待写任务完成
    hcomm.Drain(channel);
}
```
