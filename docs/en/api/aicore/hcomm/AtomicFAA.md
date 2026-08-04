# AtomicFAA

## Function Description
Submits a Fetch-and-add atomic operation task. It performs an atomic addition on the value at the remote address `dst`, and writes the original value before addition to the local `fetchAddr`.

This interface currently only supports the `COMM_PROTOCOL_UBC_CTP` path.

## Function Prototype
```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t AtomicFAA(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T addVal);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Input/Output | Remote target GM address for atomic operations. |
| `fetchAddr` | Output | Local GM address used to store the original value at the remote address before atomic addition. |
| `addVal` | Input | Value to be added to the remote target address. |

## Template Parameters
| Parameter | Description |
| --- | --- |
| `T` | Data type for atomic operations. Only `int32_t`, `uint32_t`, `int64_t`, `uint64_t` are supported. |
| `commit` | Whether to perform an immediate commit when submitting the task. |
| `commitPipe` | Pipe used for commit operations. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for requests. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration, only used for URMA path. Defaults to `URMA_DEFAULT_CFG` (strong ordering + fence + CQE enabled). |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Task submission succeeded. |
| `-1` | Task submission failed. |

## Constraints
- The communication channel must be initialized before invocation.
- The passed `ChannelHandle` must correspond to a `COMM_PROTOCOL_UBC_CTP` channel.
- `dst` must be within the range of the remote buffer registered for the channel. `fetchAddr` stores the original value with a size of `sizeof(T)`.
- A single `AtomicFAA` task occupies 2 WQE blocks in the URMA SQ.
