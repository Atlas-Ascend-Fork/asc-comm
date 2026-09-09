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

获取用于添加批量任务的BatchHandle引用。多通道模式下，本接口用于选择一个逻辑通道及其远端注册内存；单通道模式下返回传入的句柄本身。

## 函数原型

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline BatchHandle<T>& GetHandleRef(
    T& batchHandle,
    uint32_t channelIndex,
    GM_ADDR remoteAddr = nullptr);
```

## 参数说明

**表1**  模板参数说明

| 参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2**  接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄。 |
| `channelIndex` | 输入 | 多通道逻辑通道索引；单通道时忽略。 |
| `remoteAddr` | 输入 | 多通道模式下用于选择远端注册内存。传入`nullptr`时选择该逻辑通道的第一个远端注册buffer；单通道时忽略。默认值为`nullptr`。 |

## 返回值

返回用于添加批量任务的BatchHandle引用。

## 约束说明

- 多通道模式下，`channelIndex`必须小于创建`MultiChannelHandle`时指定的`channelNum`。
- 多通道模式下，`remoteAddr`非空时必须位于所选逻辑通道的一个远端注册buffer内。通过返回句柄发起的批量操作必须访问该注册buffer。
- 切换逻辑通道或远端注册buffer时，需要重新调用本接口。
- 本接口不返回状态码，调用方必须保证逻辑通道索引和远端地址有效。
- 多通道模式下，通过返回的句柄引用调用批量读、写和写通知接口；通过`MakeBatchHandle`返回的多通道批量句柄调用`BatchCommit`和批量`Drain`。
- 单通道模式下，`channelIndex`和`remoteAddr`不起作用，远端注册内存由`MakeBatchHandle`选择。

## 调用示例

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::MultiChannelHandle multiChannel; // Host侧已创建的有效多通道句柄
AscendC::LocalTensor<uint8_t> batchBuffer; // 已从UB内存中分配
GM_ADDR remoteDst; // 所选逻辑通道中已注册的远端目的地址
GM_ADDR localSrc; // 有效的本端源地址
uint32_t batchBufferLen = 64U;
uint32_t channelIndex = 0U;
uint32_t dataLen = 64U;

auto multiBatchHandle = hcomm.MakeBatchHandle(
    multiChannel, batchBuffer, batchBufferLen);
auto& batchHandle = hcomm.GetHandleRef(
    multiBatchHandle, channelIndex, remoteDst);
int32_t ret = hcomm.WriteNbi(batchHandle, remoteDst, localSrc, dataLen);
if (ret == 0) {
    ret = hcomm.BatchCommit(multiBatchHandle);
}
if (ret == 0) {
    ret = hcomm.Drain(multiBatchHandle);
}
```
