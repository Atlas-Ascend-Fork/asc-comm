# Hcomm SIMT Batch performance example

[中文](./README.md)

This example compares single-lane immediate submission (`Immediate`), single-lane batch submission
(`ExplicitBatch`), and multi-lane cooperative batch submission (`GroupBatch`)
for each of `WriteNbi`, `ReadNbi`, `WriteWithNotifyNbi`, `WriteValueNbi`, `AtomicFAA`, and `AtomicCAS`. All three
paths use identical payload sizes, WQE counts, warmup counts, and address rotation. Both batch paths call
`BatchCommit` once per group and complete through `Drain(batch)`.

Each rank pair emits one final `RESULT` containing `API`, `DataSize/B`, `Lanes`, `DBCount`, `Mode`,
`Bandwidth/GB/s`, per-operation `IssueTime/ns`, and `Status` combined from sender and receiver validation. Detailed
`FAIL_DETAIL` or `CHECK_DETAIL` lines are emitted only on failure.

The two- or eight-device communicator uses `HcclGetRootInfo`/`HcclCommInitRootInfo`; no rank table is required.
In eight-device mode, pairs 0→1, 2→3, 4→5, and 6→7 run the same configuration concurrently and report results independently.

## Baseline design

The default one-command run produces one Immediate, two ExplicitBatch configurations with batch sizes 8 and
1024, and five GroupBatch configurations with `2/4/8/16/32 lanes` per API: 48 independent cases in total. The
1024-item batch publishes one DB and directly compares with the recorded 78 ns/op SIMD batch-size-1024 result.
Every case reinitializes the communicator and data,
rotates over 64 addresses, performs 96 warmup and 1024 timed operations, and uses a 64-byte RMA/Notify payload.
Warmup is drained before timing
so setup and
warmup cannot contaminate the measurement. A performance number is valid only after device status, local
Read/Atomic fetch data, and remote target data all pass.

Use the defaults for the basic gate, `batch-size=1` and a non-multiple iteration count for boundary regression,
then sweep `batch-size=2/4/8/16/32` at least three times per configuration for optimization comparisons. Do not
combine results from different code versions; `CodeVersion` identifies the binary source.

## Basic version and optimization boundary

The basic version prioritizes reuse of the validated single-thread path from `simt_urma_after`. All six
operations stage their existing 128-byte immediate WQE layout in explicit UB. `BatchCommit` reserves SQ, copies
the WQE, and writes DWQE per item to establish the six-API correctness baseline first. It adopts the handle,
context/token caching, WQEBB/CQE counters, and batched-commit structure from the SIMD batch PR, but does not reuse
AIV-specific `LocalTensor`, `DataCopy`, `Mutex`, or scalar `st_dev` doorbell mechanisms.

Only after all six APIs pass functionally in both Immediate and ExplicitBatch modes should multiple items be
combined behind one tail-WQE publication and the following
optimizations be evaluated as separate changes:

1. Compact non-tail Write/Read and WriteValue WQEs to one BB while retaining the validated two-BB tail layout.
2. Generate intermediate WQEs directly in SQ to remove the UB-to-SQ copy.
3. Reduce repeated context loads, fences, and CQEs.
4. Finally compare batch sizes, issue throughput, and end-to-end completion time to select defaults.

These optimizations must not change the public API or the single-thread immediate path, and each must remain
independently revertible.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch_perf
bash build.sh
```

## Run

With no arguments, the script runs Immediate, ExplicitBatch, and five GroupBatch lane counts for all six APIs:

```bash
bash run.sh
```

Run a single API with custom parameters:

```bash
bash run.sh write --iterations 4096 --warmup 96 --payload-bytes 64 --batch-size 8
bash run.sh cas --iterations 4096 --batch-size 16
bash run.sh all --devices 8
```

Use `--mode immediate`, `--mode batch`, or `--mode group` to run one path. `--group-lanes` defaults to
`2,4,8,16,32`, and iterations/warmup must be divisible by every selected lane count. The current group path
prepares one item per lane, so increasing lanes also reduces DB count; interpret `Lanes` together with `DBCount`.
Run each configuration at least three times and
compare stable per-operation `IssueTime/ns` and `Bandwidth/GB/s` values.

Example output:

```text
TEST_INFO | Suite=simt_batch_perf | CodeVersion=... | ...
RESULT | API=write | DataSize/B=64 | Lanes=1 | DBCount=1 | Mode=ExplicitBatch | Bandwidth/GB/s=... | IssueTime/ns=... | Status=PASS
```
