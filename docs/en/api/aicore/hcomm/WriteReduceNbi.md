# WriteReduceNbi

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:59:44.904Z pushedAt=2026-11-09T06:47:54.980Z nl=2 -->

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

Adds a write task with a reduction operation: performs the reduction specified by `reduceOp` on the `count` elements of type `T` in the local `src` and the corresponding elements in the remote `dst`, and writes the result to the remote `dst`.

## Function Prototype

```cpp
template <
    typename T, HcommUrmaReduceOp reduceOp, bool commit = true, pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3, auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteReduceNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Data type of the reduction operation. Only `int8_t`, `int16_t`, `int32_t`, `uint32_t`, `half`, `float`, and `bfloat16_t` are supported. |
| `reduceOp` | Reduction operation type (`HcommUrmaReduceOp`). `MAX`, `MIN`, and `SUM` are supported. |
| `commit` | Whether to commit immediately when submitting the task. `false` indicates deferred submission, which is later triggered uniformly by a call with `commit` set to `true`. |
| `commitPipe` | Pipe used for commit, defaulting to `PIPE_S`. |
| `reqPipe` | Pipe used for the request, defaulting to `PIPE_MTE3`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required). |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Remote GM (Global Memory) address of the reduction target. |
| `src` | Input | Local source GM address. |
| `count` | Input | Number of elements to reduce. The total number of bytes is `count * sizeof(T)`. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | Submitted successfully. |
| `-1` | Submission failed. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- Before the call, the communication channel must be initialized and a temporary workspace must be provided through [Init](./Init.md).
- `dst` must fall within the remote registered memory range registered for the channel.
- `config.inlineEn` must be 0; otherwise, a compilation error is reported.
- A single `WriteReduceNbi` task occupies one task slot (64 bytes). When the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred-commit tasks must not exceed `sqDepth`. Commit the accumulated tasks through [Commit](./Commit.md) or automatic commit before the SQ is exhausted; otherwise, subsequent task submission fails due to SQ overflow.

## Call Example

The following shows an example of calling the **WriteReduceNbi** API. To focus on the call flow, the example omits return value checks and error handling.

```cpp
// Number of elements to reduce.
constexpr uint64_t COUNT = 256U;
// Initialize the UB temporary workspace size: it must not be less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_write_reduce_nbi_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create an Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Aggregate COUNT float elements from the local localBuffer into the remote remoteBuffer using SUM reduction.
    hcomm.WriteReduceNbi<float, AscendC::HcommUrmaReduceOp::SUM>(
        channel, remoteBuffer, localBuffer, COUNT);

    // Block until the reduction write task completes.
    hcomm.Drain(channel);
}
```
