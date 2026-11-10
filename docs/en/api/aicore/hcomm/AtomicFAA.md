# AtomicFAA

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:25:12.100Z pushedAt=2026-11-09T06:47:54.962Z nl=2 -->

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

Adds a fetch-and-add atomic operation task that atomically adds a value to the remote address `dst` and writes the old value before the addition to the local `fetchAddr`.

Currently, this API supports only the `COMM_PROTOCOL_UB_CTP` protocol.

## Function Prototype

```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t AtomicFAA(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T addVal);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Data type of the atomic operation. Only `int32_t`, `uint32_t`, `int64_t`, and `uint64_t` are supported. |
| `commit` | Whether to commit immediately when submitting the task. |
| `commitPipe` | Pipe used for commit, defaulting to `PIPE_S`. |
| `reqPipe` | Pipe used for requests, defaulting to `PIPE_MTE3`. |
| `config` | WQE control configuration, defaulting to `URMA_DEFAULT_CFG` (relax order + CQE order-preserving reporting + fence enabled + CQE reporting required). |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle. |
| `dst` | Output | Remote GM (Global Memory) address of the atomic operation target. |
| `fetchAddr` | Output | Local GM address used to store the old value before the atomic add is performed on the remote address. |
| `addVal` | Input | Value to add to the remote target address. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | Submitted successfully. |
| `-1` | Submission failed. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- Before calling this API, the communication channel must be initialized and a temporary workspace must be provided through `Init`.
- The `ChannelHandle` passed in must correspond to a `COMM_PROTOCOL_UB_CTP` channel.
- `dst` must fall within the remote registered memory range of the channel. `fetchAddr` is used to store the old value, with a length of `sizeof(T)`.
- A single `AtomicFAA` task occupies 2 task slots (128 bytes). When the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred-commit tasks must not exceed `sqDepth`. You must submit the accumulated tasks through `Commit` or automatic commit before the SQ is exhausted; otherwise, subsequent task submission will fail due to SQ overflow.
- In the deferred-commit scenario (multiple deferred commits + one final commit), only the final commit should generate a CQE (that is, `config.cqe` of intermediate tasks is set to **0**, and that of the final task is set to **1**).

## Call Example

The following shows an example of calling the **AtomicFAA** API. To focus on the call flow, the example omits return value checks and error handling.

```cpp
// Value to be atomically added.
constexpr uint64_t ADD_VAL = 1U;
// Initialize the UB temporary workspace size: must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_atomic_faa_kernel(
    uint64_t channel, GM_ADDR remoteCounter, GM_ADDR fetchBuffer)
{
    // Prepare the UB temporary workspace. alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Perform an atomic add of ADD_VAL on the remote remoteCounter, and write the old value before the addition to the local fetchBuffer.
    hcomm.AtomicFAA<uint64_t>(channel, remoteCounter, fetchBuffer, ADD_VAL);

    // Block and wait for the atomic operation task to complete.
    hcomm.Drain(channel);
}
```
