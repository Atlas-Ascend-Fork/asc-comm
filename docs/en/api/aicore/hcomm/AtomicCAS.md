# AtomicCAS

## Function Description
Submits a Compare-and-swap atomic operation task. It compares the value stored at the remote address `dst` with `compareVal`. If equal, the remote value is replaced with `swapVal`, and the original value before replacement is written to the local `fetchAddr`.

This interface currently only supports the `COMM_PROTOCOL_UBC_CTP` path.

## Function Prototype
```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t AtomicCAS(
    AscendC::ChannelHandle channel,
    GM_ADDR dst,
    GM_ADDR fetchAddr,
    T compareVal,
    T swapVal);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Input/Output | Remote target GM address for atomic operations. |
| `fetchAddr` | Output | Local GM address used to store the original value at the remote address before CAS execution. |
| `compareVal` | Input | Comparison value. |
| `swapVal` | Input | New value written to the remote target address when comparison succeeds. |

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
- A single `AtomicCAS` task occupies 2 WQE blocks in the URMA SQ.
