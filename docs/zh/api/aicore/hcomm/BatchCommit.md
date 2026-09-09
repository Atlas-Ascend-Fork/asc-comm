# BatchCommit

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

提交BatchHandle当前批次中的全部任务。当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

## 函数原型

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## 参数说明

**表1**  模板参数说明

| 参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

**表2**  接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄。成功后，当前批次可以重新添加任务。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败，例如当前批次为空、状态无效或任务数量超过允许容量。 |

## 约束说明

- 调用前需要通过批量`ReadNbi`、`WriteNbi`或`WriteWithNotifyNbi`至少添加一个任务。
- 一个批次内可以混合添加读、写和写通知任务。
- 当前批次使用的工作区不能超过创建BatchHandle时指定的`buffLen`，并且任务数量必须满足通道容量限制。
- 多通道模式下，通过`GetHandleRef`返回的句柄引用添加任务，通过`MakeBatchHandle`返回的多通道批量句柄调用本接口。
- 提交成功后，可以复用BatchHandle和工作区添加并提交下一个批次。
- 可以连续提交多个批次，再统一调用一次批量`Drain`。调用方必须保证累计未完成任务数量和完成记录数量不超过通道配置的相应容量。
- 使用BatchHandle期间必须独占其关联的通道资源，不能混用普通`ChannelHandle`提交流程。

## 调用示例

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // 已完成资源初始化的UBC_CTP通道句柄
AscendC::LocalTensor<uint8_t> batchBuffer; // 已从UB内存中分配
GM_ADDR remoteDst; // 已注册的远端目的地址
GM_ADDR localSrc; // 有效的本端源地址
uint32_t batchBufferLen = 64U;
uint32_t dataLen = 64U;

auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteDst);
int32_t ret = hcomm.WriteNbi(batchHandle, remoteDst, localSrc, dataLen);
if (ret == 0) {
    ret = hcomm.BatchCommit(batchHandle);
}
```
