# WriteWithNotifyNbi

## Function Description
Submits a point-to-point write task carrying a remote notification address and notification value.

This interface currently only supports the `COMM_PROTOCOL_UBC_CTP` path. An interface with the same name is reserved for the `COMM_PROTOCOL_ROCE` path, but its implementation will return a failure.

## Function Prototype
```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteWithNotifyNbi(
    AscendC::ChannelHandle channel,
    GM_ADDR dst,
    GM_ADDR src,
    uint64_t len,
    GM_ADDR notifyAddr,
    uint64_t notifyVal);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Destination GM address. |
| `src` | Input | Source GM address. |
| `len` | Input | Write length in bytes. |
| `notifyAddr` | Input | Remote notification address. |
| `notifyVal` | Input | Remote notification value. |

## Template Parameters
| Parameter | Description |
| --- | --- |
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
- This interface is not supported on the `COMM_PROTOCOL_ROCE` path; invocation returns `-1`.
- A single `WriteWithNotifyNbi` task occupies 2 WQE blocks in the URMA SQ.
