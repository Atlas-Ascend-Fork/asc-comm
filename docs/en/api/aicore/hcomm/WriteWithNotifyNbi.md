# write_with_notify_nbi

## Function Description
Submits a point-to-point write task carrying a remote notification address and notification value.

This interface currently only supports the `comm_protocol_ubc_ctp` path. An interface with the same name is reserved for the `comm_protocol_roce` path, but its implementation will return a failure.

## Function Prototype
```cpp
template <
    bool commit = true,
    pipe_t commit_pipe = PIPE_S,
    pipe_t req_pipe = PIPE_MTE3,
    auto const& config = urma_default_cfg>
__aicore__ inline int32_t write_with_notify_nbi(
    AscendC::channel_handle channel,
    GM_ADDR dst,
    GM_ADDR src,
    uint64_t len,
    GM_ADDR notify_addr,
    uint64_t notify_val);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Destination GM address. |
| `src` | Input | Source GM address. |
| `len` | Input | Write length in bytes. |
| `notify_addr` | Input | Remote notification address. |
| `notify_val` | Input | Remote notification value. |

## Template Parameters
| Parameter | Description |
| --- | --- |
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
- This interface is not supported on the `comm_protocol_roce` path; invocation returns `-1`.
- A single `write_with_notify_nbi` task occupies 2 WQE blocks in the URMA SQ.
