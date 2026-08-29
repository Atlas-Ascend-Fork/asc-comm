# WriteValueNbi

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol but not the RoCE protocol
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Not supported
<!-- end id7 -->

## Function Description

Submits a point-to-point immediate-value write task: `value` is carried by value in the WQE and written to the remote `dst`. Unlike `WriteNbi`, which copies data from a local `src` buffer, the source data of `WriteValueNbi` is directly provided by the `value` argument and inlined in the WQE (Inline Write). It does not depend on a local source buffer and is suitable for scenarios where a single scalar/immediate value is written.

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

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Type of the immediate `value`. Supports `int8_t`, `uint8_t`, `int16_t`, `uint16_t`, `half`, `bfloat16_t`, `int32_t`, `uint32_t`, `float`, `int64_t`, `uint64_t`, and `double`. `sizeof(T)` is the write width. |
| `commit` | Whether to commit immediately when the task is submitted. `true` indicates that the WQE is committed immediately after being assembled; `false` indicates that only the WQE is assembled and a later `Commit` call is required to submit it. Default: `true`. |
| `commitPipe` | Pipe used for commit. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for requests. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration. Default: `URMA_INLINE_CFG` (execution order: relax order + in-order CQE reporting + fence enabled + CQE reporting required + inline data carried). Immediate-value writes require `inlineEn` to be `1`; otherwise a compile error is raised. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Remote destination address. |
| `value` | Input | Immediate value to write to the remote side, carried inline in the WQE as `sizeof(T)` bytes. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Submission succeeded. |
| `-1` | Submission failed. |

## Constraints

- The communication channel must be initialized before invocation, and temporary workspace must be provided through `Init`.
- The passed `ChannelHandle` must correspond to a `COMM_PROTOCOL_UBC_CTP` channel.
- `config.inlineEn` must be `1` (that is, inline mode is used); otherwise a compile error is raised.
- If the `commit` template parameter is set to `false`, the number of consecutive calls must not exceed `sqDepth`. Pending tasks must be committed through `Commit` before the SQ is exhausted; otherwise subsequent `PostSend` fails due to SQ overflow.
- In a batched-submission scenario (multiple delayed commits + one final commit), only the last commit should produce a CQE (that is, `config.cqe` is set to `0` for intermediate tasks and `1` for the last one).

## Example

The calling example of WriteValueNbi is as follows:

```cpp
uint32_t value = 42;
hcomm.WriteValueNbi(channel, remoteAddr, value);
hcomm.Drain(channel);    // Wait for tasks to complete
```
