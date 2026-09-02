# Hcomm SIMT Batch performance example

[中文](./README.md)

This example compares single-lane immediate submission (`Immediate`) with multi-lane cooperative batch
submission (`GroupBatch`)
for each of `WriteNbi`, `ReadNbi`, `WriteWithNotifyNbi`, `WriteValueNbi`, `AtomicFAA`, and `AtomicCAS`. Both
paths use identical payload sizes, WQE counts, warmup counts, and address rotation. Group batch size equals its
lane count, each lane prepares one item, and each group calls `BatchCommit` once. `ExplicitBatch` remains only
as the batch-size-1 single-lane fixed-overhead case; it no longer aggregates multiple items in one lane.

Each rank pair emits one final `RESULT` containing `API`, `DataSize/B`, `Lanes`, `DBCount`, `Mode`,
`Bandwidth/GB/s`, per-operation `IssueTime/ns`, and `Status` combined from sender and receiver validation. Detailed
`FAIL_DETAIL` or `CHECK_DETAIL` lines are emitted only on failure.

The two- or eight-device communicator uses `HcclGetRootInfo`/`HcclCommInitRootInfo`; no rank table is required.
In eight-device mode, pairs 0→1, 2→3, 4→5, and 6→7 run the same configuration concurrently and report results independently.

## Baseline design

The default one-command run produces one Immediate and six GroupBatch configurations with
`2/4/8/16/32/128 lanes` per API.
Every case reinitializes the communicator and data,
rotates over 64 addresses, performs 96 warmup and 1024 timed operations, and uses a 64-byte RMA/Notify payload.
Warmup is drained before timing
so setup and
warmup cannot contaminate the measurement. A performance number is valid only after device status, local
Read/Atomic fetch data, and remote target data all pass.

Use the defaults for the basic gate, `--mode batch --batch-size 1` for the single-lane boundary, and
`--group-lanes` for lane scaling. Run each configuration at least three times. Do not
combine results from different code versions; `CodeVersion` identifies the binary source.

## Current submission path

`MakeBatchHandle` stores stable queue addresses and remote Jetty fields once in shared UB. Append generates
position-independent request fields in lane-private state and does not write shared UB. `BatchCommit` has lane 0
reserve the whole SQ range once, lets each lane write its final SQ slot directly, then has the tail lane publish
the batch through DWQE after all lanes finish. Write/Read/WriteValue batches use 1BB items, while
Notify/FAA/CAS batches use 2BB items; operations with different BB counts are not mixed in one batch.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch_perf
bash build.sh
```

Use `bash build.sh --profile` to report Append/Commit/Plan/Emit/Publish timing. This mode adds extra `clock()`
operations; use the default build for final performance comparisons.

## Run

With no arguments, the script runs Immediate and the default GroupBatch lane counts for all six APIs:

```bash
bash run.sh
```

Run a single API with custom parameters:

```bash
bash run.sh write --iterations 4096 --warmup 96 --payload-bytes 64 --group-lanes 2,4,8,16,32
bash run.sh cas --iterations 4096 --mode group --group-lanes 16,32,128
bash run.sh write --mode batch --batch-size 1
bash run.sh all --devices 8
```

Use `--mode immediate`, `--mode batch`, or `--mode group` to run one path. `--group-lanes` defaults to
`2,4,8,16,32,128`, and iterations/warmup must be divisible by every selected lane count. The current group path
prepares one item per lane, so increasing lanes also reduces DB count; interpret `Lanes` together with `DBCount`.
Run each configuration at least three times and
compare stable per-operation `IssueTime/ns` and `Bandwidth/GB/s` values.

Example output:

```text
TEST_INFO | Suite=simt_batch_perf | CodeVersion=... | ...
RESULT | API=write | DataSize/B=64 | Lanes=32 | DBCount=32 | Mode=GroupBatch | Bandwidth/GB/s=... | IssueTime/ns=... | Status=PASS
```
