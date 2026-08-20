# atomic_faa

## Function Description
Submits a Fetch-and-add atomic operation task. It performs an atomic addition on the value at the remote address `dst`, and writes the original value before addition to the local `fetch_addr`.

This interface currently only supports the `comm_protocol_ubc_ctp` path.

## Function Prototype
```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commit_pipe = PIPE_S,
    pipe_t req_pipe = PIPE_MTE3,
    auto const& config = urma_default_cfg>
__aicore__ inline int32_t atomic_faa(
    AscendC::channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Input/Output | Remote target GM address for atomic operations. |
| `fetch_addr` | Output | Local GM address used to store the original value at the remote address before atomic addition. |
| `add_val` | Input | Value to be added to the remote target address. |

## Template Parameters
| Parameter | Description |
| --- | --- |
| `T` | Data type for atomic operations. Only `int32_t`, `uint32_t`, `int64_t`, `uint64_t` are supported. |
| `commit` | Whether to perform an immediate commit when submitting the task. |
| `commit_pipe` | Pipe used for commit operations. Default: `PIPE_S`. |
| `req_pipe` | Pipe used for requests. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration, only used for URMA path. Defaults to `urma_default_cfg` (strong ordering + fence + CQE enabled). |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Task submission succeeded. |
| `-1` | Task submission failed. |

## Constraints
- The communication channel must be initialized before invocation.
- The passed `channel_handle` must correspond to a `comm_protocol_ubc_ctp` channel.
- `dst` must be within the range of the remote buffer registered for the channel. `fetch_addr` stores the original value with a size of `sizeof(T)`.
- A single `atomic_faa` task occupies 2 WQE blocks in the URMA SQ.
