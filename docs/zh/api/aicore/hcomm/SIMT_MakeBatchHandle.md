# MakeBatchHandle

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

创建批量句柄，并绑定批量操作所需的UB工作区。返回的批量句柄类型为`BatchHandle<ChannelHandle>`（即`UbcCtpBatchHandle`）。单通道模式直接使用返回的句柄添加任务；多线程协作模式下通过Group协作重载由组内线程共同向同一句柄添加任务，任务通过[BatchCommit](./SIMT_BatchCommit.md)提交，通过[Drain](./SIMT_Drain.md)等待完成。

## 函数原型

普通接口：

```cpp
__simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb = 1U);
```

Group协作接口：

```cpp
template <typename Group>
__simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb,
    const Group& group);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `buff` | 输入 | 调用方提供的UB批量工作区，起始地址为`__ubuf__`类型指针。 |
| `buffLen` | 输入 | 批量工作区可用长度，单位为字节。 |
| `remoteBase` | 输入 | 远端注册内存基址，用于选择本批任务访问的远端注册内存。 |
| `itemBb` | 输入 | 单个批量任务占用的WQEBB（Work Queue Element Basic Block）数量，默认值为1。`itemBb`为1与2对应不同的批量操作类型，不能在同一批量句柄下混用，见约束说明。 |
| `group` | 输入 | SIMT协作组，Group协作重载使用。 |

## 返回值说明

返回与`channel`对应的批量句柄。该接口不返回状态码，调用方必须保证参数满足约束。

## 约束说明

- `channel`必须有效，并且对应的通信资源已完成初始化。
- 批量工作区由调用方提供，`buffLen`不能超过工作区实际容量。
- BatchHandle、通道资源和`buff`的生命周期由调用方负责。BatchHandle使用完成前，不得释放或并发复用这些资源。
- 使用BatchHandle期间必须独占其关联的通道资源，不能交叉调用普通`ChannelHandle`接口，也不能并发使用其他BatchHandle。
- BatchHandle应作为不透明句柄使用，调用方不应直接访问或修改其成员。
- 同一有效批量句柄下，`itemBb`为1与`itemBb`为2的批量操作不能混用：[WriteNbi](./SIMT_WriteNbi.md)、[ReadNbi](./SIMT_ReadNbi.md)、[WriteValueNbi](./SIMT_WriteValueNbi.md)对应`itemBb`为1，[WriteWithNotifyNbi](./SIMT_WriteWithNotifyNbi.md)、[AtomicFAA](./SIMT_AtomicFAA.md)、[AtomicCAS](./SIMT_AtomicCAS.md)对应`itemBb`为2。

## 调用示例

MakeBatchHandle接口的调用示例如下。

```cpp
// 批量工作区大小
constexpr uint32_t BATCH_BUF_SIZE = 128U;

__simt_callee__ void SimtMakeBatchHandleOp(
    AscendC::ChannelHandle channel, __ubuf__ uint8_t* batchBuffer, __gm__ uint8_t* remoteBase)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区，remoteBase用于选择远端注册内存
    auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, BATCH_BUF_SIZE, remoteBase);
}
```
