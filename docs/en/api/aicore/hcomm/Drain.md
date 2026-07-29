# Drain

## Function Description
Blocks and waits until all communication tasks on the specified channel complete execution.

## Function Prototype
```cpp
template <pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t Drain(AscendC::ChannelHandle channel);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |

## Template Parameters
| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used for drain operations. Default: `PIPE_MTE3`. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Waiting succeeded. |
| `-1` | Waiting failed. |
