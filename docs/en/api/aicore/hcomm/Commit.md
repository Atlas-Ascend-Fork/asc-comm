# Commit

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:26:55.288Z pushedAt=2026-11-09T06:47:54.964Z nl=2 -->

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

Notifies that the prepared communication tasks on a specified ordinary channel can start execution. It is usually called explicitly after setting `commit` to `false` when calling ordinary `ReadNbi`, `WriteNbi`, `WriteValueNbi`, `WriteWithNotifyNbi`, `WriteReduceNbi`, `AtomicFAA`, or `AtomicCAS`.

This API accepts only a `ChannelHandle`. Submit tasks in a **BatchHandle** through [BatchCommit](./BatchCommit.md).

## Function Prototype

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `pipe` | Pipe used by commit, defaulting to `PIPE_S`. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Ordinary communication channel handle. |

## Return Value

**Table 3** Return values

| Return Value | Description |
| --- | --- |
| `0` | Submitted successfully. |
| `-1` | Submission failed. |

## Usage Constraints

- The `COMM_PROTOCOL_UB_CTP` and `COMM_PROTOCOL_ROCE` protocols are supported.
- Call `Init` to provide temporary workspace for common APIs before this API.
- If the `commit` template parameter is set to `false`, the total number of slots occupied by accumulated deferred-commit tasks must not exceed `sqDepth`. Submit the accumulated tasks through `Commit` or automatic commit before the SQ is exhausted; otherwise, subsequent task submission fails due to SQ overflow.
- In a deferred-commit scenario (multiple deferred commits + one final commit), only the final commit should generate a completion record. For `COMM_PROTOCOL_UB_CTP` tasks, `config` is the task configuration passed to each communication API, and its `cqe` field controls whether the task generates a completion record. Set `config.cqe` of intermediate tasks to **0** and that of the last task to **1**.
- Tasks in a **BatchHandle** can only be submitted through [BatchCommit](./BatchCommit.md). Do not mix ordinary `Commit` and `BatchCommit` on the same channel.

## Call Example

The following shows an example of calling the **Commit** API. To focus on the call flow, the example omits return value checks and error handling. For a complete sample, see [07_hcomm_roce_normal_write_read](../../../../../examples/aicore/hcomm/07_hcomm_roce_normal_write_read/README.md).

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Size of the initialized UB temporary workspace: must be no less than 512 bytes.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_commit_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // Prepare the UB temporary workspace. alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // Create the Hcomm object and initialize the UB temporary workspace.
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // Set commit = false when submitting the task to only assemble the task without submitting it immediately.
    hcomm.WriteNbi<false>(channel, remoteBuffer, localBuffer, BYTES);

    // Explicitly submit the tasks accumulated on the channel.
    hcomm.Commit(channel);

    // Block and wait for the submitted tasks to complete.
    hcomm.Drain(channel);
}
```
