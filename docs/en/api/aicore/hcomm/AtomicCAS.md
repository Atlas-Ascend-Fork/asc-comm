# AtomicCAS

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

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Data type of the atomic operation. Only `int32_t`, `uint32_t`, `int64_t`, and `uint64_t` are supported. |
| `commit` | Whether to commit immediately when the task is submitted. |
| `commitPipe` | Pipe used for commit. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for requests. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG` (execution order: relax order + in-order CQE reporting + fence enabled + CQE reporting required). |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Input/Output | Remote target address of the atomic operation. |
| `fetchAddr` | Output | Local address used to store the old value at the remote address before CAS is executed. |
| `compareVal` | Input | Comparison value. |
| `swapVal` | Input | New value written to the remote target address when the comparison succeeds. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Submission succeeded. |
| `-1` | Submission failed. |

## Constraints

- The communication channel must be initialized before invocation.
- The passed `ChannelHandle` must correspond to a `COMM_PROTOCOL_UBC_CTP` channel.
- `dst` must fall within the range of the remote buffer registered for the channel, `fetchAddr` is used to store the old value, and the length is `sizeof(T)`.
- A single `AtomicCAS` task occupies 2 WQE blocks in the URMA SQ.
- If the `commit` template parameter is set to `false`, the number of consecutive calls must not exceed `sqDepth`. Pending tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise subsequent `PostSend` fails due to SQ overflow.
- In a batched-submission scenario (multiple delayed commits + one final commit), only the last commit should produce a CQE (that is, `config.cqe` is set to `0` for intermediate tasks and `1` for the last one).

## Example

The calling example of AtomicCAS is as follows:

```cpp
int32_t compareVal = 0;
int32_t swapVal = 1;
hcomm.AtomicCAS(channel, remoteAddr, fetchAddr, compareVal, swapVal);
hcomm.Drain(channel);    // Wait for tasks to complete
```
