# BatchCommit

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->

## Function Description

Submits all tasks in the current batch of a BatchHandle. This API currently supports only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

## Function Prototype

```cpp
template <
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t BatchCommit(T& batchHandle);
```

## Parameter Description

**Table 1**  Template Parameter Description

| Parameter | Description |
| --- | --- |
| `T` | Batch handle type deduced from `batchHandle`. |

**Table 2**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `batchHandle` | Input/Output | Single-channel or multi-channel batch handle created by `MakeBatchHandle`. After success, tasks can be added to a new batch. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Submission succeeds. |
| `-1` | Submission fails, for example because the current batch is empty, its state is invalid, or its task count exceeds the allowed capacity. |

## Constraints

- Add at least one task through batch `ReadNbi`, `WriteNbi`, or `WriteWithNotifyNbi` before calling this API.
- Read, write, and write-with-notify tasks can be mixed in one batch.
- The workspace used by the current batch must not exceed the `buffLen` specified when creating the BatchHandle, and the number of tasks must satisfy the channel capacity limit.
- In multi-channel mode, add tasks through the handle reference returned by `GetHandleRef` and call this API with the multi-channel batch handle returned by `MakeBatchHandle`.
- After a successful submission, reuse the BatchHandle and workspace to add and submit another batch.
- Multiple batches can be submitted before one batch `Drain`. The caller must ensure that accumulated incomplete tasks and completion records do not exceed their corresponding configured channel capacities.
- While using a BatchHandle, exclusively own its associated channel resources. Do not mix the ordinary `ChannelHandle` submission workflow.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // Initialized UBC_CTP channel handle
AscendC::LocalTensor<uint8_t> batchBuffer; // Allocated from UB memory
GM_ADDR remoteDst; // Registered remote destination address
GM_ADDR localSrc; // Valid local source address
uint32_t batchBufferLen = 64U;
uint32_t dataLen = 64U;

auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteDst);
int32_t ret = hcomm.WriteNbi(batchHandle, remoteDst, localSrc, dataLen);
if (ret == 0) {
    ret = hcomm.BatchCommit(batchHandle);
}
```
