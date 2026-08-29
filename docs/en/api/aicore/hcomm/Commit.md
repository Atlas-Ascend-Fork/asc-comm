# Commit

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Supports the RoCE protocol but not the URMA protocol
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Supports the RoCE protocol but not the URMA protocol
- Atlas inference series products Vector Core: Supports the RoCE protocol but not the URMA protocol
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Supports the RoCE protocol but not the URMA protocol
<!-- end id7 -->

## Function Description

Notifies the specified ordinary channel that its prepared communication tasks can start execution. It is usually called explicitly after setting `commit` to `false` when calling ordinary `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, or `AtomicCAS`.

This interface accepts only a `ChannelHandle` and does not submit the WQEs in a BatchHandle. Call [BatchCommit](./BatchCommit.md) for the batch interface.

## Function Prototype

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for commit. Default: `PIPE_S`. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Ordinary communication channel handle. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Submission succeeded. |
| `-1` | Submission failed. |

## Constraints

- Call `Init` to provide temporary workspace for ordinary interfaces before this interface.
- If the `commit` template parameter is set to `false`, the number of consecutive calls must not exceed `sqDepth`. Pending tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise subsequent `PostSend` fails due to SQ overflow.
- In a batched-submission scenario (multiple delayed commits + one final commit), only the last commit should produce a CQE (that is, `config.cqe` is set to `0` for intermediate tasks and `1` for the last one).
- WQEs in a BatchHandle can be submitted only through `BatchCommit`. Ordinary `Commit` and `BatchCommit` use different queue state management and must not be mixed on the same channel.

## Example

The calling example of Commit is as follows:

```cpp
// Deferred commit: prepare multiple tasks first, then commit them together
hcomm.WriteNbi<false>(channel, dst1, src1, len1);
hcomm.WriteNbi<false>(channel, dst2, src2, len2);
hcomm.Commit(channel);    // Commit them together
hcomm.Drain(channel);     // Wait for completion
```
