# Hcomm SIMT Batch validation and issue timing

Validates WriteNbi, ReadNbi, WriteWithNotifyNbi, WriteValueNbi, AtomicFAA and AtomicCAS for functionality and performance.

- Immediate: individual requests posted directly from one lane.
- ExplicitBatch: one lane executes Append → BatchCommit with a fixed batch size of 1.
- GroupBatch: multiple lanes each build one request, one BatchCommit per batch; the batch size equals the lane count.

Each check verifies the device-returned status and data; Read checks local data, Atomic checks the fetch return value and the remote result, and Notify checks both data and the notify value. 2 or 8 cards are supported; with 8 cards they are paired as 0→1, 2→3, 4→5, 6→7, and each pair outputs its result separately.

## Build and Execution

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/06_simt_batch_perf
bash build.sh

# Single-lane Write → BatchCommit → Drain and remote data validation.
bash run.sh write --mode batch --batch-size 1
# Compare single submission against Group Batch; by default the core runs only Write/Read/Notify.
bash run.sh
# Run all six interfaces; by default compare includes Immediate and GroupBatch, not ExplicitBatch.
bash run.sh all
# Specify the interface, grouping, and device count.
bash run.sh cas --mode group --group-lanes 16,32,128
bash run.sh all --mode batch --batch-size 1 --devices 8
```

By default 64 addresses rotate, with 96 warmup and 1024 timed operations, and a 64B RMA/Notify payload. GroupBatch scans 2/4/8/16/32/128 lanes by default; the timed request count must be divisible by the lane count, and warmup is rounded up to a whole batch. More than 128 lanes require compiling with `bash build.sh --full`.

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

A BatchHandle is bound to the remote registration containing `remote`; subsequent target addresses must lie within the same region. The final `1U` specifies the per-item BB count for an ordinary Write.

Group calls use the same Hcomm object without storing a Group, and pass the group explicitly at the end of each batch operation:

```cpp
// The call below is joined by all lanes in the group; each lane builds its own request.
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U, group);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remoteForLane, localForLane, 64U, group) != 0) return;
if (hcomm.BatchCommit(batch, group) != 0) return;
// After all posting lanes return, one lane calls Drain; Drain takes no group.
```

The same Group (identical members, ranks and size) and the same UB workspace must be used from creation to commit of a handle. The no-group overloads are for single-lane handles; a size-1 Group uses single-lane dispatch. Write/Read/Notify/WriteValue/FAA/CAS all accept the group as the final argument, with the explicit config template parameters before the group. When running the example, install the run package matching this repository's source and recompile the example.

## Output

The same format as `07_multi_jetty_batch_perf`, one line per rank pair per item:

```text
RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
```

The primary metric `IssueTicksPerRequest` = the device `clock()` delta over the timed submission interval / the request count; lower is better. It includes construction, submission, and submission-path synchronization, and excludes the subsequent `Drain` wait; the unit is raw ticks, not converted to ns. Only compare PASS rows, and do not compute a speedup directly against SIMD data whose clock units are not aligned.

BatchSize is the request count per batch; Jettys and ConcurrentJettys are the configured count and the actual maximum parallel count respectively; Pair is the rank pair. In this single-Jetty example both are 1. The example runs all functionality checks, normally only summarizes results, and expands rank logs on failure. Completion bandwidth, doorbell counts, and per-item diagnostics are not output by default.

## RESULT fields

| Field | Meaning |
|---|---|
| API | Communication operation such as write, read or notify |
| Mode | Immediate: single-lane direct submission; ExplicitBatch: single-lane explicit Batch; GroupBatch: multiple lanes cooperating on one Jetty; MultiJetty: multiple-Jetty parallel example |
| DataSize/B | Data bytes per request, excluding Notify flags and other control information |
| BatchSize | Requests per Jetty per batch; GroupBatch/MultiJetty build one item per lane, so it also equals the lane count per Jetty |
| Jettys | Total number of Jettys configured for the rank pair |
| ConcurrentJettys | Maximum number of Jettys participating in submission in a single kernel; e.g. J8/BS256 is 4, requiring two kernels |
| IssueTicksPerRequest | Total submission-interval ticks divided by the total request count; lower is better; raw clock() counts, not converted to ns |
| Pair | The communicating rank pair, e.g. 0-1; with 8 cards there are four pairs |
| Status | Whether both ends' execution and the correctness checks for the case pass; PASS or FAIL |

IssueTicksPerRequest measures the average submission cost, not the end-to-end latency of a single request. It includes construction/publication and submission-path synchronization, excludes the subsequent Drain wait; warmup is not counted, and submission intervals are accumulated across kernels and divided by the total request count, excluding host launch gaps.

A successful build prints no compilation progress; a normal run prints only RESULT and existing summaries. Compilation output is saved to build-output.log under the build directory, and the run configuration is recorded in the `.metadata` file next to the summary log. On failure, the last 40 lines of the compilation log or the corresponding rank log are shown automatically; these files do not need to be relayed back normally.