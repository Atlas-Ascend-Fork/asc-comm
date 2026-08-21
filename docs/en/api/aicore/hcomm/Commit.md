# Commit

## Function Description

Notifies an ordinary channel that its prepared communication tasks can start execution. It is normally called after setting `commit` to `false` on ordinary `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, or `AtomicCAS` calls.

This interface accepts only a `ChannelHandle` and does not submit WQEs stored in a BatchHandle. Use [BatchCommit](./BatchCommit.md) for the batch workflow.

## Function Prototype

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Ordinary communication channel handle. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for commit. Default: `PIPE_S`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The tasks are committed successfully. |
| `-1` | The commit fails. |

## Constraints

- Call `Init` to provide temporary workspace for ordinary interfaces before this interface.
- WQEs in a BatchHandle can be submitted only by `BatchCommit`. Ordinary `Commit` and `BatchCommit` manage queue state differently and must not be mixed on the same channel.
