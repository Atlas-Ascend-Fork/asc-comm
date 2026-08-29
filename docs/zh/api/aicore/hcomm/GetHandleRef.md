# GetHandleRef

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
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->

## 功能说明

返回用于执行批量操作的BatchHandle引用。单通道时原样返回传入句柄；多通道时选择一个逻辑通道和远端注册内存。

## 函数原型

```cpp
template <
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline BatchHandle<ChannelHandle>& GetHandleRef(
    T& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr = nullptr);
```

## 参数说明

**表1**  模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2**  接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄。 |
| `channelIndex` | 输入 | 多通道逻辑通道索引；单通道时忽略。 |
| `remoteAddr` | 输入 | 多通道远端已注册buffer内地址；为空时选择该通道的第一个远端已注册buffer。单通道时忽略。默认值为`nullptr`。 |

## 返回值说明

单通道返回输入句柄本身；多通道返回配置为所选逻辑通道和远端MR的内层`BatchHandle<ChannelHandle>&`。

## 约束说明

- 单通道时`channelIndex`和`remoteAddr`不改变句柄，远端内存仍由`MakeBatchHandle`选择。
- 多通道时，`channelIndex`必须小于创建`MultiChannelHandle`时传入的`channelNum`。
- 多通道时，`remoteAddr`非空时必须落在所选逻辑通道的一个远端已注册buffer中；为空时选择该通道的第一个远端已注册buffer。
- 本接口不返回状态码，调用方必须保证多通道的逻辑通道索引和远端MR地址有效。
- 对同一个多通道批量句柄多次调用本接口时，返回的引用均指向同一个内层BatchHandle；每次调用会更新该内层句柄当前选择的逻辑通道和远端MR信息。
- 多通道返回引用的批量操作必须使用本次调用缓存的token；访问使用不同token的远端buffer前需要再次调用本接口。
- 多通道使用返回的内层引用调用批量读、写和写通知；将外层多通道批量句柄传入`BatchCommit`和批量`Drain`。

## 调用示例

GetHandleRef接口的调用示例如下。

```cpp
auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferLen);
auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, channelIndex, peerRemoteAddr);
hcomm.WriteNbi(peerBatchHandle, dst, src, len);    // 通过内层引用准备WQE
hcomm.BatchCommit(multiBatchHandle);    // 通过外层句柄提交
hcomm.Drain(multiBatchHandle);    // 通过外层句柄等待完成
```
