# MakeBatchHandle

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

创建批量句柄，并绑定批量操作所需的UB工作区。当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径。

调用侧建议使用`auto`接收返回值。单通道模式直接使用返回的句柄添加和提交任务；多通道模式通过`GetHandleRef`选择逻辑通道后添加任务。

## 函数原型

```cpp
template <typename T, typename U>
__aicore__ inline BatchHandle<T> MakeBatchHandle(
    T channel,
    const AscendC::LocalTensor<U>& buff,
    uint32_t buffLen,
    GM_ADDR remoteAddr = nullptr,
    GM_ADDR localAddr = nullptr);
```

## 参数说明

**表1**  模板参数说明

| 参数 | 说明 |
| --- | --- |
| `T` | 通道句柄类型，支持`ChannelHandle`和`MultiChannelHandle`。 |
| `U` | `LocalTensor`的元素类型。 |

**表2**  接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | UBC_CTP单通道`ChannelHandle`，或Host侧`MakeMultiChannelHandle`创建的`MultiChannelHandle`。 |
| `buff` | 输入 | 调用方提供的UB `LocalTensor`，作为批量操作的工作区。 |
| `buffLen` | 输入 | 工作区可用长度，单位为字节。 |
| `remoteAddr` | 输入 | 单通道模式下用于选择远端注册内存。传入`nullptr`时选择第一个远端注册buffer；多通道模式下忽略。默认值为`nullptr`。 |
| `localAddr` | 输入 | 保留参数。默认值为`nullptr`。 |

## 返回值

返回与`channel`对应的`BatchHandle<T>`。该接口不返回状态码，调用方必须保证参数满足约束。

## 约束说明

- 仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`。
- `channel`必须有效，并且对应的通信资源已完成初始化。
- 单通道模式下，`remoteAddr`非空时必须位于一个远端注册buffer内。后续批量操作访问的远端地址必须属于该注册buffer。
- 多通道模式下，通过`GetHandleRef`选择逻辑通道和远端注册内存。
- `buff`起始地址必须按32字节对齐。
- `buffLen`不能小于64字节，不能超过`buff.GetSize() * sizeof(U)`，并且必须小于`sqDepth * 64`字节。不能被64整除的剩余空间不可用。
- BatchHandle、通道资源、`MultiChannelHandle`和`buff`的生命周期由调用方负责。BatchHandle使用完成前，不得释放或并发复用这些资源。
- 使用BatchHandle期间必须独占其关联的通道资源，不能交叉调用普通`ChannelHandle`接口，也不能并发使用其他BatchHandle。
- BatchHandle应作为不透明句柄使用，调用方不应直接访问或修改其成员。

## 调用示例

单通道模式：

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // 已完成资源初始化的UBC_CTP通道句柄
AscendC::LocalTensor<uint8_t> batchBuffer; // 已从UB内存中分配
GM_ADDR remoteAddr; // 已注册的远端地址
uint32_t batchBufferLen = 64U;

auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteAddr);
```

多通道模式：

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::MultiChannelHandle multiChannel; // Host侧已创建的有效多通道句柄
AscendC::LocalTensor<uint8_t> batchBuffer; // 已从UB内存中分配
uint32_t batchBufferLen = 64U;

auto multiBatchHandle = hcomm.MakeBatchHandle(
    multiChannel, batchBuffer, batchBufferLen);
```
