# Hcomm SIMT Batch performance example

[中文](./README.md)

This example compares ordinary immediate submission (`Immediate`) with the explicit batch path (`ExplicitBatch`)
for each of `WriteNbi`, `ReadNbi`, `WriteWithNotifyNbi`, `WriteValueNbi`, `AtomicFAA`, and `AtomicCAS`. Both paths
use identical payload sizes, WQE counts, warmup counts, and address rotation. The explicit path calls
`BatchCommit` once per group and completes through `Drain(batch)`.

Every `KERNEL_RESULT` identifies `Rank`, `Peer`, `API`, and `Mode` and reports:

- `PublishCount`: actual hardware publications. The current correctness baseline reuses the validated immediate
  DWQE publication path for every staged item;
- `IssueTime/us` and `AverageIssue/ns`: submission cost;
- `CompletionTime/us`: end-to-end time including Drain;
- `CompletionBandwidth/GB/s`: completion bandwidth based on useful bytes;
- `StatusCode`, `FailurePhase`, `FailureAction`, and `FailureOperation`: distinguish the exact Immediate,
  Append, BatchCommit, or Drain operation that failed;
- `DeviceStatus` and `LocalCheck`: kernel/local-fetch results only, never a substitute for the remote check.

The odd rank in every pair then prints the remote `CHECK`. Only after every pair succeeds does the script print the authoritative
`CASE | API=... | Mode=... | Devices=... | Status=PASS`. FAA/CAS also validate the final fetched old value in every slot, and
the remote FAA expectation is derived from the actual operation count. A mismatch prints the first failing slot,
field, Actual, and Expected values.

The two- or eight-device communicator uses `HcclGetRootInfo`/`HcclCommInitRootInfo`; no rank table is required.
In eight-device mode, pairs 0→1, 2→3, 4→5, and 6→7 run the same configuration concurrently and report results independently.

## Baseline design

The default one-command run produces 12 independent cases: `6 APIs × 2 modes`. Every case reinitializes the
communicator and data, rotates over 64 addresses, performs 100 warmup and 1024 timed operations, uses a 64-byte
RMA/Notify payload, and calls `BatchCommit` for every eight explicit-batch items. Warmup is drained before timing
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

With no arguments, the script runs Immediate and ExplicitBatch for all six APIs:

```bash
bash run.sh
```

Run a single API with custom parameters:

```bash
bash run.sh write --iterations 4096 --warmup 100 --payload-bytes 64 --batch-size 8
bash run.sh cas --iterations 4096 --batch-size 16
bash run.sh all --devices 8
```

Use `--mode immediate` or `--mode batch` to run only one path, and `--log FILE` to select a log path. Logs go to
`results/` by default. `--devices` accepts 2 or 8 and defaults to 2; `--batch-size` accepts 1 through 32. Run each configuration at least three times and
compare stable `AverageIssue/ns` and `CompletionTime/us` values.

Example output:

```text
TEST_INFO | Suite=simt_batch_perf | CodeVersion=... | ...
KERNEL_RESULT | Rank=0 | Peer=1 | API=write | Mode=ExplicitBatch | ... | DeviceStatus=PASS | LocalCheck=PASS
CHECK | Rank=1 | Peer=0 | API=write | Mode=ExplicitBatch | PASS
CASE | API=write | Mode=ExplicitBatch | CodeVersion=... | Devices=8 | Pairs=4 | Status=PASS
```
