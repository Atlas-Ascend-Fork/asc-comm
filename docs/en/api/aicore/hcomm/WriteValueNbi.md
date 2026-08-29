# WriteValueNbi

## Function Description

Submits a point-to-point immediate-value write task: `value` is carried by value in the WQE and written to the remote `dst`. Unlike `WriteNbi`, which copies data from a local `src` buffer, `WriteValueNbi` takes its source data directly from the `value` argument and inlines it in the WQE (Inline Write). It does not depend on a local source buffer and is suitable for writing a single scalar/immediate value.

This interface currently only supports the `COMM_PROTOCOL_UBC_CTP` path.

## Function Prototype

```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_INLINE_CFG>
__aicore__ inline int32_t WriteValueNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, T value);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Remote destination address. |
| `value` | Input | Immediate value to write to the remote side, inlined in the WQE as `sizeof(T)` bytes. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `T` | Type of the immediate `value`. Supports `int8_t`, `uint8_t`, `int16_t`, `uint16_t`, `half`, `bfloat16_t`, `int32_t`, `uint32_t`, `float`, `int64_t`, `uint64_t`, `double`. `sizeof(T)` is the write width. |
| `commit` | Whether to commit the task immediately on submission. `true` assembles the WQE and commits immediately; `false` only assembles the WQE and requires a later `Commit`. Default: `true`. |
| `commitPipe` | Pipe used for commit. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for the request. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration. Default: `URMA_INLINE_CFG` (execution order: relax order + in-order CQE reporting + fence enabled + CQE reporting required + inline data carried). Immediate-value writes require `inlineEn` to be `1`; otherwise a compile error is raised. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Task submission succeeded. |
| `-1` | Task submission failed. |

## Constraints

- The communication channel must be initialized, and `Init` must provide temporary workspace before this interface is called.
- The `ChannelHandle` must identify a `COMM_PROTOCOL_UBC_CTP` channel.
- `config.inlineEn` must be `1` (inline mode); otherwise a compile error is raised.
- If `commit` is set to `false`, the number of consecutive calls must not exceed `sqDepth`. Pending tasks must be submitted via `Commit` or automatic commit before the SQ is exhausted; otherwise subsequent `PostSend` calls fail due to SQ overflow.
- In a batched-submission scenario (multiple delayed commits followed by a final commit), only the last commit should produce a CQE (i.e. `config.cqe` is `0` for intermediate tasks and `1` for the last one).
