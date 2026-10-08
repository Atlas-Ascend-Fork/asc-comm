# Multi-Jetty SIMT engineering validation

This engineering validation program depends on internal Hcomm completion interfaces and is not a user API example. It uses the Hcomm headers installed under `$ASCEND_HOME_PATH`. Install the run package that matches this source tree, then build the program with `--clean`.

This example publishes multiple Jettys concurrently and uses one consumer to attribute shared-CQ completions to their JFS owners. It supports Write, Read and WriteWithNotify.

```bash
# Source your CANN environment first. Build exactly one batch specialization.
bash build.sh --batch-size 1
# Write/Read/Notify x J1/J2/J4/J8: twelve cases.
bash run.sh --batch-size 1
# Optional single case.
bash run.sh --batch-size 1 --api write --jetty-counts 4
# Repeat build/run for batches 8,32,64,128,256 to cover all 72 cases.
```

`--batch-size` selects both requests per batch and lanes per Jetty, with one request per lane. `--jetty-counts` selects the Jetty counts. Lane count cannot be configured independently. Fixed geometry: 64B, 1024 timed requests, 96 warmup requests rounded up to a batch, two ranks.

Each artifact contains one parallel VF. `build.sh` gives CMake configuration and compilation 300 seconds to complete. After a successful build, it reads the VF resource record for that batch/Jetty geometry, requires stack usage no greater than 1152B, and records register usage. Layout=matrix-v4, batch size and binary SHA must match the completed build. Incremental builds reuse resource records only for the identical object. `--clean` removes only the selected build directory. See `--help` for supported options.

## File responsibilities

| File | Responsibility |
|---|---|
| main.cpp | Buffer allocation, kernel launch, result collection and cleanup |
| host_setup.h | Jetty creation and remote-memory index resolution |
| simt_kernel.cpp | Parallel VF lifecycle, warmup, timing and Drain |
| simt_submit.h | Three-barrier publication sequence and per-lane SQ maintenance |
| simt_completion.h | CompletionSet initialization, continuation, windows and snapshots |
| validation.h | Completion counts, payload bytes and Notify value checks |
| multi_jetty_batch_perf_common.h | Shared constants and host/device data layout |

Each lane maintains its own SQ writes. Three block barriers coordinate preparation, publication and the next batch, so groups wait at those boundaries. Single-consumer CQ ownership does not serialize Jetty publication.

J8/BS256 uses two stream-ordered kernels of four Jettys with shared completion state. Jettys and ConcurrentJettys report the configured and maximum concurrent counts. Read validates the initiator; Write/Notify validate the receiver; every warmup/timed payload and Notify flag is checked.

Each batch reports 12 RESULT rows and one MATRIX_SUMMARY. Full per-rank logs are stored alongside the summary. Validation failures are collected; timeout/signal/setup failures abort. RESULT uses the same fields as `06_simt_batch_perf`: API, Mode, DataSize/B, BatchSize, Jettys, ConcurrentJettys, IssueTicksPerRequest, Pair and Status. IssueTicksPerRequest uses raw clock() ticks for submission, including posting synchronization and excluding the subsequent Drain wait. Successful runs omit detailed completion snapshots; all checks run and rank logs contain the details. Clock units must be aligned before comparing against SIMD.

MakeBatchHandle and every batch Write/Read/Notify call take group explicitly. Each handle uses fixed group membership and one UB workspace. Publication uses three barriers, and one consumer manages the shared CQ.

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
