# WriteWithNotifyNbi

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

将数据从本端`src`写入远端`dst`，并携带远端通知地址和通知值。接口提供普通`channel`重载、批量`batchHandle`重载和批量Group协作重载：普通重载直接向通道提交写通知任务；批量重载将写通知任务添加到当前批次，后续由[BatchCommit](./SIMT_BatchCommit.md)统一提交。

## 函数原型

普通接口：

```cpp
template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal);
```

批量接口：

```cpp
template <auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal);
```

批量Group协作接口：

```cpp
template <auto const& config = URMA_DEFAULT_CFG, typename Group>
__simt_callee__ inline int32_t WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal, const Group& group);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `commit` | 普通接口是否在提交任务时立即commit。`false`表示延迟提交，后续由`commit`为`true`的调用统一触发。 |
| `config` | URMA任务配置，默认为`URMA_DEFAULT_CFG`。 |
| `Group` | SIMT协作组类型，由批量Group协作重载使用。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄。 |
| `dst` | 输出 | 远端目的GM地址。 |
| `src` | 输入 | 本端源GM地址。 |
| `len` | 输入 | 写入长度，单位为字节。 |
| `notifyAddr` | 输入 | 远端通知GM地址。 |
| `notifyVal` | 输入 | 远端通知值。 |
| `group` | 输入 | SIMT协作组，批量Group协作重载使用。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或写通知任务成功添加到当前批次。 |
| 非`0` | 操作失败。 |

## 约束说明

**普通接口**

- 调用前通信通道资源需已完成初始化。

**批量接口**

- 添加完成后需要调用[BatchCommit](./SIMT_BatchCommit.md)提交当前批次。
- `dst`访问区间和`notifyAddr`需要属于批量句柄选中的同一远端注册内存。

## 调用示例

WriteWithNotifyNbi接口的调用示例如下。

普通接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 写任务完成后写入远端通知地址的通知值
constexpr uint64_t NOTIFY_VAL = 1U;

__simt_callee__ void SimtWriteNotifyOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* localBuffer, __gm__ uint8_t* remoteBuffer,
    __gm__ uint8_t* notifyAddr)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 本端localBuffer数据写入远端remoteBuffer，任务完成后向远端notifyAddr写入通知值
    hcomm.WriteWithNotifyNbi(channel, remoteBuffer, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);

    // 阻塞等待写通知任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 写任务完成后写入远端通知地址的通知值
constexpr uint64_t NOTIFY_VAL = 1U;
// 批量工作区大小
constexpr uint32_t BATCH_BUF_SIZE = 128U;

__simt_callee__ void SimtBatchWriteNotifyOp(
    AscendC::ChannelHandle channel, __ubuf__ uint8_t* batchBuffer, __gm__ uint8_t* localBuffer,
    __gm__ uint8_t* remoteBase, __gm__ uint8_t* notifyAddr)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区，remoteBase用于选择远端注册内存
    auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, BATCH_BUF_SIZE, remoteBase);

    // 向当前批次添加写通知任务
    hcomm.WriteWithNotifyNbi(batchHandle, remoteBase, localBuffer, BYTES, notifyAddr, NOTIFY_VAL);

    // 提交当前批次中的全部任务
    hcomm.BatchCommit(batchHandle);

    // 等待已提交任务生成的完成记录
    hcomm.Drain(batchHandle);
}
```
