# Commit

## Function Description
Notifies the specified channel that submitted communication tasks can start execution. It is typically invoked explicitly when `commit` is set to `false` in calls to `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA` or `AtomicCAS`.

## Function Prototype
```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |

## Template Parameters
| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for commit operations. Default: `PIPE_S`. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Task submission succeeded. |
| `-1` | Task submission failed. |
