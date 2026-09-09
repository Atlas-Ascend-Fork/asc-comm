# Commit

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

通知指定普通通道上已准备的通信任务可以开始执行。通常在调用普通`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`或`AtomicCAS`时将`commit`设置为`false`后显式调用。

该接口只接受`ChannelHandle`。BatchHandle中的任务需要通过[BatchCommit](./BatchCommit.md)提交。

## 函数原型

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## 参数说明

**表1**  模板参数说明

| 参数 | 说明 |
| --- | --- |
| `pipe` | commit使用的pipe，默认`PIPE_S`。 |

**表2**  接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 普通通信通道句柄。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前需要通过`Init`为普通接口提供临时工作区。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`或自动commit提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。
- BatchHandle中的任务只能通过`BatchCommit`提交，不能在同一通道上混用普通`Commit`和`BatchCommit`。

## 调用示例

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> tmpBuffer; // 已从UB内存中分配
AscendC::ChannelHandle channel; // 已完成资源初始化的UBC_CTP通道句柄
GM_ADDR remoteDst; // 已注册的远端目的地址
GM_ADDR localSrc; // 有效的本端源地址
uint32_t tmpLen = 512U;
uint64_t dataLen = 64U;

int32_t ret = hcomm.Init(tmpBuffer, tmpLen);
if (ret == 0) {
    ret = hcomm.WriteNbi<false>(channel, remoteDst, localSrc, dataLen);
}
if (ret == 0) {
    ret = hcomm.Commit(channel);
}
```
