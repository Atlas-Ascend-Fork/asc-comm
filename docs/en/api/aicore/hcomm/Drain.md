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
| Non-zero | Waiting failed. The `COMM_PROTOCOL_UBC_CTP` path returns the underlying CQ polling error code directly: `0xFF` indicates a polling timeout, while other positive values combine the CQE status and substatus. Other protocols may return `-1`. |
