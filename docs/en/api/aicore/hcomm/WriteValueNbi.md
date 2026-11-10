# WriteValueNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T10:00:33.767Z pushedAt=2026-11-09T06:47:54.981Z nl=2 -->

## Supported Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference products: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training products: Not supported
<!-- end id6 -->

## Function Description

Header file path: `"hcomm/hcomm.h"`.

Adds a point-to-point immediate write task: `value` is carried in the WQE and written to the remote `dst`. Unlike `WriteNbi`, which copies data from the local `src` buffer, `WriteValueNbi` obtains the source data directly from the `value` argument and inlines it in the WQE (Inline Write), without relying on a local source buffer. It is suitable for writing a single scalar or immediate.

Currently, this API supports only the `COMM_PROTOCOL_UB_CTP` protocol.

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

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Type of the immediate `value`. Supported types: `int8_t`, `uint8_t`, `int16_t`, `uint16_t`, `half`, `bfloat16_t`, `int32_t`, `uint32_t`, `float`, `int64_t`, `uint64_t`, and `double`. `sizeof(T)` is the write width. |
| `commit` | Whether to commit immediately when the task is submitted. `true` commits the WQE immediately after it is assembled; `false` only assembles the WQE, which must be committed later by calling [Commit](./Commit.md). The default value is `true`. |
| `commitPipe` | Pipe used for commit. The default value is `PIPE_S`. |
| `reqPipe` | Pipe used for the request. The default value is `PIPE_MTE3`. |
| `config` | WQE control configuration. The default value is `URMA_INLINE_CFG` (relax order + CQE in-order reporting + fence enabled + CQE reporting required + inline data carried). Immediate-value write requires `inlineEn` to be `1`. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Remote destination address. |
| `value` | Input | Immediate value to be written to the remote end, carried inline in the WQE as `sizeof(T)` bytes. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | Task submission succeeded. |
| `-1` | Task submission failed. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- The communication channel must be initialized, and `Init` must provide temporary workspace before this API is called.
- The `ChannelHandle` passed in must correspond to a `COMM_PROTOCOL_UB_CTP` channel.
- `config.inlineEn` must be `1` (inline mode); otherwise a compile error is raised.
- A single `WriteValueNbi` task occupies one task slot (64 bytes). When the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred tasks must not exceed `sqDepth`. The accumulated tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise, subsequent task submission fails due to SQ overflow.
- In the deferred commit scenario (multiple deferred commits + one final commit), only the final commit should generate a CQE (that is, `config.cqe` of the intermediate tasks is set to **0**, and that of the final task is set to **1**).

## Call Example

The following example shows how to call the **WriteValueNbi** API. To focus on the call flow, the example omits return value checks and error handling.

```cpp
// Write the immediate to the remote address.
constexpr uint32_t VALUE = 0x5A5A5A5AU;
// Initialize the UB temporary workspace size: no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_value_nbi_kernel(
    uint64_t channel, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Inline-write the immediate VALUE to the start address of the remote remoteBuffer.
    hcomm.WriteValueNbi(channel, remoteBuffer, VALUE);

    // Block and wait for the write task to complete.
    hcomm.Drain(channel);
}
```
