# Hcomm SIMT Batch validation and issue timing

Validates WriteNbi, ReadNbi, WriteWithNotifyNbi, WriteValueNbi, AtomicFAA and AtomicCAS.

Immediate posts individual requests from one lane. ExplicitBatch uses one lane and batch size 1. GroupBatch uses one request per lane and one BatchCommit per group; batch size equals lane count. Device status, remote data, local Read data, atomic fetch results and Notify flags are checked as applicable. Two or eight devices are supported; eight devices form four independent rank pairs.

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/06_simt_batch_perf
bash build.sh
# Single-lane Write/BatchCommit/Drain validation.
bash run.sh write --mode batch --batch-size 1
# Default core APIs: Write/Read/Notify, Immediate and GroupBatch.
bash run.sh
# All six APIs. ExplicitBatch is selected separately.
bash run.sh all
bash run.sh cas --mode group --group-lanes 16,32,128
bash run.sh all --mode batch --batch-size 1 --devices 8
```

Defaults: 64 rotating addresses, 96 warmup requests and 1024 timed requests, 64B RMA/Notify payload. Default groups are 2/4/8/16/32/128 lanes. Timed requests must be divisible by group size; warmup is rounded up. Groups above 128 lanes require `bash build.sh --full`.

## Minimal API sequence

```cpp
constexpr uint32_t bytes = 256U; // 128B shared context + 128B publisher DWQE image
alignas(128) __ubuf__ uint64_t workspace[bytes / sizeof(uint64_t)];
AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0U) != 0) return;
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remote, local, 64U) != 0) return;
if (hcomm.BatchCommit(batch) != 0) return;
if (hcomm.Drain(batch) != 0) return;
```

All batch destinations must belong to the remote registration containing remote. The final 1U specifies the per-item BB count for ordinary Write.

Group calls use Hcomm without storing a Group and pass group explicitly as the last argument of each batch operation:

```cpp
// Every lane in group participates and prepares its own request.
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U, group);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remoteForLane, localForLane, 64U, group) != 0) return;
if (hcomm.BatchCommit(batch, group) != 0) return;
// After all posting lanes return, one lane calls Drain; Drain takes no group.
```

Use the same Group (membership, ranks and size) and shared UB workspace throughout a handle's lifetime. Overloads without group are for single-lane handles; a Group of size 1 uses single-lane dispatch. Write/Read/Notify/WriteValue/FAA/CAS accept group as the last function argument, after explicit config template arguments. Install the run package that matches this source tree before rebuilding the example.

## Output

Both batch performance examples emit one RESULT per rank pair and case:

```text
RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
```

IssueTicksPerRequest is the device clock() delta over submission divided by the request count. It includes construction, posting and posting synchronization, and excludes the following Drain wait. Lower is better. These are raw ticks, not nanoseconds; only PASS rows are valid. Do not calculate a SIMD speedup until clock units are aligned.

BatchSize is requests per batch. Jettys is the configured count and ConcurrentJettys is the maximum concurrent count; both are 1 here. Pair identifies the ranks. The example runs all correctness checks and prints rank logs on failure. Completion bandwidth, doorbell counts and diagnostic breakdowns are omitted from normal output.

## RESULT fields

| Field | Meaning |
|---|---|
| API | Communication operation, such as write, read or notify |
| Mode | Immediate: single-lane direct posting; ExplicitBatch: single-lane explicit batch; GroupBatch: lanes cooperating on one Jetty; MultiJetty: concurrent-Jetty example |
| DataSize/B | Data bytes per request, excluding Notify flags and control information |
| BatchSize | Requests per Jetty per batch; GroupBatch/MultiJetty use one request per lane |
| Jettys | Total configured Jettys for the rank pair |
| ConcurrentJettys | Maximum Jettys participating in one kernel; J8/BS256 uses 4 per kernel and two kernels |
| IssueTicksPerRequest | Total submission ticks divided by total requests; lower is better, raw clock() ticks, not ns |
| Pair | Communicating rank pair; eight devices form four pairs |
| Status | PASS/FAIL from process exits and applicable correctness checks |

The metric measures average posting cost, not per-request end-to-end latency. It includes construction, publication and posting synchronization, excludes subsequent Drain waits and warmup, and sums submission intervals across kernels without host launch gaps.

Successful builds are silent; normal runs print RESULT and existing summaries only. Compiler output stays in build-output.log under the build directory; run configuration stays in the summary log's `.metadata` companion file. Build failures print the last 40 compiler-log lines, and runtime failures show the relevant rank logs. Successful runs do not require these files to be relayed.
