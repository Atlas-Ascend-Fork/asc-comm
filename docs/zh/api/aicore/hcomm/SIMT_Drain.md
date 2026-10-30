# Drain

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

头文件路径：`"hcomm/hcomm_simt.h"`。

等待已提交的通信任务完成。接口提供普通`channel`重载和批量`batchHandle`重载：普通重载阻塞等待普通通道上已提交任务完成；批量重载等待批量句柄已提交任务生成的完成记录。

## 函数原型

普通接口：

```cpp
template <auto pipe = 0>
__simt_callee__ inline int32_t Drain(ChannelHandle channel);
```

批量接口：

```cpp
template <auto pipe = 0>
__simt_callee__ inline int32_t Drain(UbcCtpBatchHandle& batchHandle);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `pipe` | 该参数在SIMT路径下不使用，仅为兼容SIMD路径接口形态而保留，默认值为0。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| `-1` | 等待失败。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 必须由单个线程调用，且在所有提交任务的线程返回之后再调用。

**普通接口**

- 该重载等待普通`channel`调用链产生的完成记录。

**批量接口**

- 调用前必须通过[BatchCommit](./SIMT_BatchCommit.md)提交当前批次中的全部任务。
- 使用批量句柄期间需要独占其关联的通道资源，不能混用普通`Drain`。

## 调用示例

Drain接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[04_simt_hcomm_urma](../../../../../examples/aicore/hcomm/04_simt_hcomm_urma/README.md)与[06_simt_hcomm_urma_batch_perf](../../../../../examples/aicore/hcomm/06_simt_hcomm_urma_batch_perf/README.md)。

普通接口：

```cpp
__simt_callee__ void SimtDrainOp(AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer,
    __gm__ uint8_t* remoteBuffer)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 提交写任务
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, 1024U);

    // 阻塞等待普通通道上已提交任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void SimtBatchDrainOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBase)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t hcommLocal[BATCH_BUF_SIZE];

    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batchHandle = hcomm.MakeBatchHandle(channel, hcommLocal, BATCH_BUF_SIZE);

    // 提交第一批任务
    hcomm.WriteNbi(batchHandle, remoteBase, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // 复用批量句柄提交下一批任务
    hcomm.WriteNbi(batchHandle, remoteBase + BYTES, localBuffer, BYTES);
    hcomm.BatchCommit(batchHandle);

    // 等待累计提交任务生成的全部完成记录
    hcomm.Drain(batchHandle);
}
```
