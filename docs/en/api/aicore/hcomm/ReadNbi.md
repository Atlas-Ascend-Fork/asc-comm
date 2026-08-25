# ReadNbi

## Function Description
Submits a point-to-point read task via the specified communication channel to read data from `src` to `dst`.

## Function Prototype
```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t ReadNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Destination GM address. |
| `src` | Input | Source GM address. |
| `len` | Input | Read length in bytes. |

## Template Parameters
| Parameter | Description |
| --- | --- |
| `commit` | Whether to perform an immediate commit when submitting the task. |
| `commitPipe` | Pipe used for commit operations. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for requests. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration, only used for the URMA path. Defaults to `URMA_DEFAULT_CFG` (strong ordering + fence + CQE enabled). |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Task submission succeeded. |
| `-1` | Task submission failed. |

## Constraints
- The communication channel must be initialized before invocation.
- Under the `COMM_PROTOCOL_UBC_CTP` path, `src` must be within the range of the remote buffer registered for the channel, and `dst` is the local destination address.

## Related Sample
Refer to the two-card read/write workflow for AIV direct-driven URMA in [hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README_en.md).
