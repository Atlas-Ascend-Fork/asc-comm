# Commit

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

Notifies an ordinary channel that its prepared communication tasks can start execution. It is normally called after setting `commit` to `false` on ordinary `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, or `AtomicCAS` calls.

This interface accepts only a `ChannelHandle`. Submit tasks in a BatchHandle through [BatchCommit](./BatchCommit.md).

## Function Prototype

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## Parameter Description

**Table 1**  Template Parameter Description

| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for commit. Default: `PIPE_S`. |

**Table 2**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Ordinary communication channel handle. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The tasks are committed successfully. |
| `-1` | The commit fails. |

## Constraints

- Call `Init` to provide temporary workspace for ordinary interfaces before this interface.
- Tasks in a BatchHandle can be submitted only by `BatchCommit`. Do not mix ordinary `Commit` and `BatchCommit` on the same channel.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> tmpBuffer; // Allocated from UB memory
AscendC::ChannelHandle channel; // Initialized UBC_CTP channel handle
GM_ADDR remoteDst; // Registered remote destination address
GM_ADDR localSrc; // Valid local source address
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
