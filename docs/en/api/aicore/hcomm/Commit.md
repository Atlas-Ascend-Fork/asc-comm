# Commit

## Function Description

Notifies an ordinary channel that its prepared communication tasks can start execution. It is normally called after setting `commit` to `false` on ordinary `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, or `AtomicCAS` calls.

This interface accepts only a `ChannelHandle`. Submit tasks in a BatchHandle through [BatchCommit](./BatchCommit.md).

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
- If the `commit` template parameter is set to `false`, the accumulated task slots must not exceed the channel task-submission capacity. Submit the accumulated tasks through `Commit` or automatic commit before the capacity is exhausted; otherwise, subsequent task submission fails.
- In a batched-submission scenario (multiple deferred commits followed by a final commit), only the final commit should generate a completion record. For URMA tasks, `config` is the task configuration passed to each communication interface, and its `cqe` field controls whether the task generates a completion record. Set `config.cqe` to `0` for intermediate tasks and to `1` for the final task.
- Tasks in a BatchHandle can be submitted only by `BatchCommit`. Do not mix ordinary `Commit` and `BatchCommit` on the same channel.
