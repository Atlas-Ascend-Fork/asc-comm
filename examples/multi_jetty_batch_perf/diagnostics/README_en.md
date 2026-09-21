# Historical diagnostics / 历史诊断

当前验证使用上级目录。此目录只用于历史诊断与SIMD复现；旧命令路径需要加 diagnostics/，不支持 matrix profile。

# Hcomm SIMD / SIMT multi-Jetty batch benchmark

[中文](./README.md)

This example compares SIMD sequential submission and SIMT group submission to multiple Jettys on one AI Core.
It covers `WriteNbi`, `ReadNbi`, and `WriteWithNotifyNbi`. Each rank creates one HcclComm and acquires multiple
Channels to the same peer together. Each active BatchHandle exclusively owns one Channel and separate data regions.
Two or eight devices are supported without a rank table; eight-device runs report pairs 0→1, 2→3, 4→5, and 6→7 separately.

## Current full validation matrix (CompletionV1, 72 cases)

Use `simt-matrix-bs1/8/32/64/128/256`, not the legacy simt-full path.
Write/Read/Notify x J1/2/4/8 x BS1/8/32/64/128/256: 72 cases with 64B payload,1024 timed requests per Jetty, warmup96 rounded to the batch size, two devices.
Each build contains one parallel VF (capacities32/64/256/512/1024/1024), no SIMD or SIMT-Serial. Retain the5min build and1152B/32register gates per artifact.

All cases bind this checkout's Hcomm and CompletionV1, preserving the three publication boundaries and the removed duplicate SQ scan.
One CompletionSet spans all launches in a case. Initialize all Jetty identities once; later stream-ordered launches admit their own windows and resume the CQ cursor.
J8/BS256 remains two kernels with four concurrent Jettys each; output reports KernelLaunches=2 and MaxConcurrentJettys=4. Other cases use one kernel.
Validate CQ snapshots at each launch boundary, including earlier completed launches. Write/Read use requests+requests/batch BBs; Notify uses2*requests.
Check every warmup/timed payload, the initiator's Read data, and every Notify flag.

Identity: AuditVersion=10, CompletionVersion=1, MatrixBatch=<N>, Profiling=0.
Per-CQE clocks and raw CQ sampling are disabled; original issue/completion clocks and full correctness checks remain.
Measurement=RawDeviceTicks uses ticks and bytes/tick; do not equate these with archived SIMD ns/GB/s labels without calibration.
Each batch produces12 RESULT rows and one MATRIX_SUMMARY. Functional failures are collected across that batch and produce a nonzero exit; stop later tasks on timeout/device failure.
Require72 unique results for full coverage. Reuse the54 archived SIMD cases; no archived J1 comparison exists.

```bash
# Example for one batch; the run card contains all six qrun groups.
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --matrix-batch 32
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-core-parallel bash run.sh simt-parallel --build-profile simt-matrix-bs32 --api all --batch-sizes 32 --jetty-counts 1,2,4,8 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

Use the [current run card](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md).
Local host checks do not establish CANN build success, hardware correctness, resource use or performance.


## Build

Use an Ascend 950 target with CANN/ASC for dav-3510 and matching Hcomm headers/runtime.
For daily SIMT diagnostics, select the SIMT-only fast profile:

~~~bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/multi_jetty_batch_perf
bash build.sh --backend simt --fast
~~~

| Build command | Profile / output directory | Compiled kernels |
|---|---|---|
| bash build.sh --backend simt --scope control | simt-scope-control / build/simt-scope-control/ | One BS256/capacity1024 VF for A/N; full UB layout |
| bash build.sh --backend simt --scope publisher | simt-scope-publisher / build/simt-scope-publisher/ | One separately compiled candidate VF; leaves control untouched |
| bash build.sh --backend simt --fast | simt-fast / build/simt-fast/ | Two SIMT parallel VFs, BS32/256; no SIMD source |
| bash build.sh --backend simt | simt-full / build/simt-full/ | Full SIMT dispatch; no SIMD source |
| bash build.sh --backend simd | simd-full / build/simd-full/ | SIMD only; no SIMT source |
| bash build.sh --fast | all-fast / build/all-fast/ | SIMD plus two SIMT parallel VFs |
| bash build.sh | all-full / build/all-full/ | Both complete backends |

Run with --build-profile selecting the same profile. For a SIMT-only profile, run.sh all runs only SIMT;
unavailable explicit modes are rejected before device initialization. Fast profiles default to BS32/256
when --batch-sizes is omitted and do not contain SIMT-Serial.

Each profile has its own build directory. --clean removes only that profile; ordinary builds reuse its outputs.
Existing build/multi_jetty_batch_perf is preserved. Without --build-profile, run.sh uses all-full if present,
otherwise the legacy binary; it never guesses a SIMT-only profile. Each build prints its profile and elapsed seconds;
each run logs binary configuration/path/hash. The binary supports --build-info without initializing a device.

BUILD_JOBS controls build-system parallelism. The old runtime R1/R2 expansion is retired; P is compile-time isolated;
this change excludes an unselected backend, not those internal branches. Actual target build-time savings are pending.
[CMakeLists.txt](CMakeLists.txt) loads SDK headers/libraries from ASCEND_HOME_PATH. Updating repository Hcomm headers
still requires selecting the matching installed/private headers before rebuilding.

The [fixed SIMD baseline](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/reference/simd_performance_baseline.md)
contains the 2026-09-07 full recorded multi-Jetty matrix (54 PASS rows). Reuse matching configurations during SIMT
iterations; do not rebuild or rerun SIMD by default. The baseline uses 1024 timed requests, not the 512-request debug runs.

## Run

### Optional ACL configuration (historical diagnostics)

Ordinary profiles can pass a JSON path to aclInit through MULTI_JETTY_ACL_CONFIG; unset/empty uses the default.
acl_simt_stack_2048.json is retained for the historical354/334 investigation, not this experiment.
Scope profiles reject nonempty ACL configuration and enabled trace. Each scope artifact needs its own resource report;
the historical1176-byte report does not describe the restored VF.

A compact regression matrix with the current ordering control, also supported by fast builds:

```bash
unset MULTI_JETTY_BATCH_TRACE
MULTI_JETTY_PARALLEL_CONTROL=serial-commit \
    bash run.sh simt-parallel --build-profile simt-fast --api write --batch-sizes 32,256 \
    --jetty-counts 4,8 --iterations 512 --warmup 96 --payload-bytes 64
```

This is a diagnostic/regression command, not a claim that the source has passed target validation.
Only when a new SIMD baseline is needed, explicitly build both backends and compare matching configurations:

```bash
bash build.sh
MULTI_JETTY_PARALLEL_CONTROL=serial-commit \
    bash run.sh all --build-profile all-full --api all --batch-sizes 32,256 --jetty-counts 4,8 --iterations 512
```

With a full build, run the default matrix or select a mode:

```bash
bash build.sh
unset MULTI_JETTY_PARALLEL_CONTROL MULTI_JETTY_BATCH_TRACE
bash run.sh all --build-profile all-full
bash run.sh simd-serial --build-profile all-full --api notify --batch-sizes 32 --jetty-counts 4
bash run.sh simt-serial --build-profile all-full --api read --batch-sizes 32 --jetty-counts 4
bash run.sh simt-parallel --build-profile all-full --api write --batch-sizes 32 --jetty-counts 4 --devices 8
```

| Mode | Execution |
|---|---|
| `simd-serial` | SIMD builds requests and submits each Jetty's batches sequentially |
| `simt-serial` | One SIMT group processes Jettys sequentially, constructing each batch cooperatively |
| `simt-parallel` | One SIMT group per Jetty; groups progress concurrently unless a diagnostic control is selected |
| `all` | Runs compiled backends; SIMT-only profiles skip SIMD. Add `--include-simt-serial` only for full SIMT profiles |

Currently SIMT lanes per Jetty equal BatchSize, with exactly one request per lane per batch.
`--iterations` counts timed requests per Jetty, not batches; timed batches per Jetty equal iterations / BatchSize.
There is no separate `--lanes-per-jetty` option. Repeated existing Append calls from one lane would reuse its reserved
slot; they do not create multiple independent requests in that batch.

A parallel kernel uses at most 1024 lanes. BS256/J8 therefore uses two successive kernels on the same stream,
four Jettys each, reported as `2KernelLaunches`. It does not run eight Jettys simultaneously or assign a core per Jetty.

## Options

| Option | Default | Meaning |
|---|---|---|
| `--build-profile` | all-full if present, otherwise legacy | Explicitly select all-full/all-fast/simt-full/simt-fast/simd-full; never falls back to another profile |
| `--api` | `all` | `write`, `read`, `notify`, or `all` |
| `--batch-sizes` | `1,8,32,64,128,256` | Select from this set; every value must divide iterations |
| `--jetty-counts` | `1,2,4,8` | Select peer Channel counts from this set |
| `--iterations` | `1024` | Positive timed request count per Jetty |
| `--warmup` | `96` | Nonnegative request count, rounded up to a whole batch separately for each BatchSize |
| `--payload-bytes` | `64` | Positive payload byte count per request |
| `--devices` | `2` | `2` or `8` |
| `--log` | `results/<mode>_<timestamp>.log` | Captures stdout and stderr |
| `--include-simt-serial` | Off | Includes SIMT-Serial in `all` |

Requested warmup=96 becomes 96 requests for BS32 and 256 requests for BS256.

## Isolated serial-scope experiment

The question is whether ordinary lanes can Append outside the cross-Jetty ordered region. The former
serial-publisher-commit and commit-only runtime selectors are retired and rejected. P code never enters the A/N VF.

| Schedule | Profile | MULTI_JETTY_PARALLEL_CONTROL | Behavior |
|---|---|---|---|
| A | simt-scope-control | serial-commit | Restored624abdd paired Append+Commit ordering |
| N | Same binary as A | Empty/unset | Original parallel Jettys; reproduces the original failure |
| P | simt-scope-publisher | publisher-commit | Ordinary Append in parallel, publisher Append+whole Group Commit per Jetty turn |

Both profiles accept only Write/64B/BS256/J4,1024 timed requests,warmup96 (rounded256),two devices,trace off and default ACL.
Each artifact instantiates one parallel `<256,1024>` VF and uses the full UB layout, not the fast-build guards.
A/N VF/helper source is restored to624abdd; only outer dispatch is pruned. Historical machine-code identity is not claimed.

Build from this example directory. Stop on any failure or timeout:

```bash
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope control
sha256sum build/simt-scope-control/multi_jetty_batch_perf > /tmp/multi-jetty-scope-control.sha256
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope publisher
sha256sum -c /tmp/multi-jetty-scope-control.sha256
```

The build checks exactly one selected VF, stack<=1152 bytes and used registers<=32.1152 is a conservative experiment
gate, not a queried allocation. Missing/excessive reports fail with READY=NO; do not increase the stack or build the full matrix.
An unchanged device-object hash permits incremental report reuse. scope-build.log records output; scope-resource.txt records
the gate. run.sh verifies the binary hash approved by that gate. READY=YES is runtime eligibility, not correctness PASS.
Scope builds default to two build jobs. Control and candidate outputs never overwrite each other or existing profiles.

Single-case forms (use the current device card for the actual bounded order):

```bash
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=serial-commit bash run.sh simt-parallel --build-profile simt-scope-control --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL= bash run.sh simt-parallel --build-profile simt-scope-control --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-commit bash run.sh simt-parallel --build-profile simt-scope-publisher --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

P has one untimed block rendezvous after all Handle initialization. Every turn and end-of-batch retains its block
rendezvous, including failed groups; every lane of the selected group enters Commit. Both warmup and timed phases use P.
Handle/Drain, library fences/DCCI, WQE/head arithmetic remain unchanged. No SQ/Batch trace is inserted; the host rejects trace.
Error uniformity relies on the current Append returning the shared reservationStatus. The installed group BatchPostWqe must
retain210c066's noncollective Append; the historical collective Append is incompatible. No library or SDK change is required.

Run at most five A/N/P triplets. Any A failure or new execution exception stops the comparison; original failures in P reject
its sufficiency; no reproduction in N makes the outcome inconclusive. Only if A/P pass and N reproduces do we compare A/P
performance. Five passes are not long-term stability proof; about10% lower issue cost is the provisional continuation target.
Reuse the fixed SIMD baseline. Keep batch/lane geometry unchanged. Further Commit decomposition needs a new result-based decision.

## Q: prepare ordinary SQ maintenance in parallel

`--scope sq-prepare` builds the separate `simt-scope-sq-prepare` artifact. It requires
`MULTI_JETTY_PARALLEL_CONTROL=publisher-sq-prepare` and reports `Schedule=Q`.
Parameters, workspace layout, lanes, Handle/Drain and resource gates match P. Reuse the existing P binary;
no installed SDK header replacement is needed.

After ordinary Appends, each Jetty performs its ordinary producer flushes, then its publisher scans those
255 ordinary BBs in parallel with other Jettys. Publisher Append, its own producer flush, the remaining
two-BB scan, head maintenance, DWQE publication and next-batch reservation remain continuously ordered.
The two scans still cover all257 BBs. No maintenance calls are removed. Preparation adds two group
rendezvous and publisher completion fences; original Commit synchronization and five block barriers per
batch remain. Both warmup and timed paths are covered; failed groups still attend every block barrier.

The split lives only in the example's Q compile branch. Its publication tail is taken from the35335c7
Group Commit and reuses existing internal functions. It adds no public API; P contains no Q code.
Both scans retain SQ wraparound arithmetic; WQE/head algorithms are unchanged.

```bash
# From the example directory, build only Q. Stop on failure/timeout; require exit0 and READY=YES.
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope sq-prepare
# One Q case. Use the current run card's qrun form in the queued environment.
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-sq-prepare bash run.sh simt-parallel --build-profile simt-scope-sq-prepare --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

At most seven cases: existing P once, Q five times, existing P once;180s/case and15min total execution,
excluding queue time. No A/N/SIMD reruns. Any P/Q failure or new execution exception stops the comparison.
After all pass, compare the Q median with both P runs and the historical P range. A10% IssueTime reduction
is a provisional benefit target; five passes do not prove long-term stability. Q data failure rejects
this candidate, not identifies a particular DCCI as the cause. Follow the current run card; no extra rounds.

## R: remove ordered publisher turns

The current experiment uses the passing Q artifact as its control and removes only cross-Jetty publisher turns.
`--scope parallel-publish` builds the separate `simt-scope-parallel-publish` artifact, requiring
`MULTI_JETTY_PARALLEL_CONTROL=publisher-parallel` and reporting `Schedule=R`.
After ordinary SQ preparation, all Jettys enter their own publisher Append and publication concurrently.
Block barriers decrease from five to two per batch. Q's PrepareOrdinarySq and CommitPreparedOrdinarySq are reused
unchanged, with Group passed explicitly to helpers. No public Hcomm API changes are included.
Fences/DCCI, group synchronization, workspace, WQE/head, Handle/Drain and both execution phases are retained.

```bash
# From the example directory, build only R and retain existing Q. Stop on failure, timeout or READY=NO.
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope parallel-publish
# Single R case; follow the current run card for Q1/R1-R5/Q2 order and qrun commands.
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-parallel bash run.sh simt-parallel --build-profile simt-scope-parallel-publish --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

Keep Write64B, BS256, 256 lanes per Jetty and J4; batch/lane decoupling is outside this experiment.
At most seven cases: existing Q once, R five times, existing Q once; 180s per rank and 15min total execution
excluding queue time. Stop on any failure or execution exception. Only after all pass, compare the R median
with both Q controls and the historical 128ns; about 10% reduction is a provisional benefit target.
Do not rebuild Q or rerun SIMD. Passing R supports removal of publisher mutual exclusion in this configuration;
preparation and batch boundaries still synchronize Jettys. Failure does not identify a particular publication instruction.
Use the [current run card](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md)
for the authoritative procedure; do not add rounds automatically.

## S: order only the publisher's core publication work

The current candidate uses passing Q as its control. `--scope publish-core` builds the separate
`simt-scope-publish-core` artifact, requiring `MULTI_JETTY_PARALLEL_CONTROL=publisher-core` and reporting `Schedule=S`.
Ordinary Append/PrepareOrdinarySq remain unchanged. Only the publisher executes its Append, SQ maintenance,
head update and complete DWQE publication inside the turn. Three group barriers are removed from this region;
all original DCCI/fences remain. Next-batch reservation and the original two group cleanup barriers move outside
the turn loop. All five block barriers per batch remain; the last waits for all cleanup before the next batch.

Each batch freezes the group-uniform reservation status before the turns, avoiding divergent collective entry
when the publisher changes the next reservation. Current noncollective Append fails only on this shared status;
retain the previously confirmed 210c066 installed headers. Both warmup and timed phases use S. Public Hcomm APIs,
Group storage, SDK, workspace, WQE/head arithmetic and batch/lane mapping are unchanged.

```bash
# Build only S from the example directory; retain existing Q. Stop on failure, timeout or READY=NO.
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope publish-core
# One S case; use the current run card for Q1/S1/Q2 order and qrun commands.
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-core bash run.sh simt-parallel --build-profile simt-scope-publish-core --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

One 256/1024 VF; retain stack<=1152B and registers<=32 gates. Run Q1,S1,Q2 first and stop on any failure.
Only if all pass and S1 costs at most 90% of the faster Q control, add S2/S3. At most five cases and 10min actual
execution excluding queue time, 180s per rank. Stop on insufficient benefit; do not rebuild Q or rerun A/N/P/R/SIMD.
S tests the combined smaller boundary, not independent necessity of each barrier or fully independent Jetty progress.
The [current run card](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md)
is the sole authoritative procedure.

## Other diagnostic controls

Ordinary profiles retain empty (parallel),serial-commit (whole Append+Commit ordered),and one-active controls.
Scope profiles accept only the combinations in the table above.

Use `one-active` for single-kernel comparisons such as BS32/J4 or J8. Its current request
accounting/validation across multiple kernel launches is not a valid acceptance criterion for that control.

```bash
MULTI_JETTY_PARALLEL_CONTROL=one-active \
    bash run.sh simt-parallel --build-profile simt-fast --api write --batch-sizes 32 --jetty-counts 8 --iterations 512
```

Unset `MULTI_JETTY_BATCH_TRACE`, or set it to `0`, to disable device tracing. `1` enables Batch/CQ records;
`full` adds per-request SQ records; `expert` captures full records with more selective diagnostic output.
Tracing adds device work and uses a diagnostic CQ polling path, so it must be disabled for performance comparisons.
Workspace canary checks require a fast build with tracing enabled.

```bash
MULTI_JETTY_PARALLEL_CONTROL=serial-commit MULTI_JETTY_BATCH_TRACE=expert \
    bash run.sh simt-parallel --build-profile simt-fast --api write --batch-sizes 32 --jetty-counts 4 --iterations 512
```

`MULTI_JETTY_BATCH_PERF_TIMEOUT_SECONDS` controls each rank process's timeout, defaulting to 180 seconds.

## Results

Each configuration and rank pair emits a final `RESULT`. Timing starts after warmup Drain; initialization and warmup
are excluded. For multiple launches, kernel intervals are summed without host launch gaps.

| Field | Meaning |
|---|---|
| `RUN_IDENTITY` | Script checkout HEAD/tree state; alone it does not identify installed headers or the binary |
| `LanesPerJetty` | 1 for SIMD, BatchSize for SIMT |
| `BatchesPerJetty` | Timed batches per Jetty |
| `TotalDBs` | Kernel-reported timed batch doorbells; normally Jettys × iterations / BatchSize for a complete successful matrix |
| `IssueTime/ns` | Average submission time per timed request, including synchronization and any enabled tracing in that interval |
| `Bandwidth/GB/s` | Total effective payload bytes divided by time from submission start through final Drain, excluding Notify flag bytes |
| `Status` | Combined sender completion and receiver data/Notify validation; Read validates the initiator's local result |

Use only PASS rows for performance comparison. `FailureKind=RemoteValidation` identifies remote validation failure;
`CqeStatus=NA` means no matching failed CQE record was captured, not a verified zero CQE status.
Keep API, payload, Jetty count, BatchSize, control, tracing, and build configuration fixed, and repeat measurements.

## Files

- [main.cpp](./main.cpp): communication resources, launch partitioning, validation, and result aggregation.
- [simd_kernel.cpp](./simd_kernel.cpp): sequential SIMD batch submission.
- [simt_kernel.cpp](./simt_kernel.cpp): SIMT serial/parallel execution, controls, and timing.
- [run.sh](./run.sh): matrix, rank processes, logging, and final status.

## Shared CQ completion ownership: CompletionV1 (publish-audit)

The L3 candidate builds on passing 13d9c91 and removes the publisher's second SQ scan from comm Group
BatchCommit and the example's PrepareOrdinarySq, CommitPreparedOrdinarySq and PublishPreparedPublisherSq.
Each producing lane still maintains its own SQ. All fences, group/block barriers, head/DWQE publication
and completion processing stay unchanged. Write/BS256 removes 257 DCCI calls per Jetty per batch.
The three cross-Jetty boundaries remain. Shared Q/R/S helpers also change, so rebuilt artifacts cannot
serve as old binary controls. Existing Progress instrumentation remains enabled. Only the example
enables the hooks, using the final256 unused bytes of its existing4096B workspace, outside comm state.
COMPLETION_PROFILE separates Warmup/Timed calls, probes, not-ready probes, processed entries and probe,
not-ready-probe, successful-accounting, acknowledgement and total Progress ticks. NotReadyProbeTicks is a
subset of ProbeTicks; failed paths are not used for performance attribution. ProgressTicks excludes the
final diagnostic accumulation. COMPLETION_PROFILE_TAIL reports the original post-issue interval and its
remainder outside Progress, including accumulation, dispatch and synchronization. Per-CQE clocks perturb
timing; these are diagnostic distributions, not benchmark gains. PROFILE_CHECK requires1024warmup and4096timed
processed entries and consistent counters/times. Warmup is subtracted from cumulative records on the host.
Reuse simt-scope-publish-audit, one256/1024 VF, AuditVersion=9, CompletionVersion=1,
Schedule=S-parallel-3barriers. The runner rejects old binaries. CMake stages this checkout's Hcomm headers
under the profile's comm-headers directory and binds the SIMT entry using an absolute-path macro;
implementation headers resolve relative to that entry. It checks the Completion version immediately and
rejects an earlier Hcomm include instead of silently using the SDK implementation. Build through build.sh to refresh them;
the installed SDK/runtime and other profiles are unchanged.

One caller-selected lane owns all CompletionSet operations; the set is passed explicitly to functions:

| API | Contract |
|---|---|
| InitCompletionSet | Initialize once from fresh unused channels and zeroed128B-aligned GM workspace; group actual CQs and full20-bit JFS IDs |
| PrepareCompletion(set, channelIndex, wqeCount, bbCount) | Admit one window before publishing; check this SQ's space and aggregate shared-CQ capacity; only this channel's previous window must have completed |
| Progress(set, budget, processed) | Process at most budget ready CQEs, without waiting for an unready entry; attribute each completion and round-robin independent CQs |
| WaitCompletion(set, channelIndex) | Wait for this channel's admitted target while progressing CQs; other channels need not finish |
| Drain(set) | Optional wait for all admitted targets; used at the example's warmup/final completion boundaries |

A channel releases its own SQ up to its registered BB endpoint only after its whole window succeeds.
A slow Jetty does not prevent a fast one from completing, reclaiming and admitting its next window.
There is no background progress: callers schedule the owner, and must not call set operations concurrently.
Do not mix these CQs with legacy Drain, single-request ReservePost automatic polling, SIMD or another consumer.
The first integration covers default CQE-enabled Write batches, exactly the admitted counts, one window per
channel. Sparse CQEs, unpublished deferred tails and unsignalled NOP tails are unsupported. Keep state across
warmup/timing; after a fault, terminate the instance rather than clearing its cursor and retrying. Batch size
and lanes per Jetty remain coupled. Users supply channels, not CQ addresses, IDs or a Jetty publication order.
CompletionSet does not modify the producer group's UB reservation context. If an old batch's next reservation
already failed before reclamation, recreate it with MakeBatchHandle after this Jetty completes, respecting its
group contract. The current short five-batch case does not reach that boundary.

Build only this profile within five minutes, retaining stack<=1152B/registers<=32. Run the fixed Write64/J4/BS256,
1024timed+256actualwarmup case with publisher-core-parallel, legacy trace off and default ACL configuration.
Run exactly one control case and return PASS or FAIL; do not repeat automatically. Do not rebuild
Q/S/SIMD. Exact commands are in the current DesignDocs run card; no new remote scripts.

COMPLETION_STATE must show each channel's warmup count256/sharedCQ1024/SQ257, then completed/target1280,
sharedCQconsumed/acknowledged5120 and SQtail/targetBB1285. COMPLETION_CHECK makes any mismatch fail automatically;
RequestIdentityV1 additionally checks all5120 payloads. COMPLETION_ERROR retains the actual CQ sequence, full
JFS/Jetty, rawthree words and hardware status/substatus, including errors outside the old1280-entry snapshot.
Kinds:0 submission/admission,1 resource,2 identity,3 hardwareCQE,4 count/endpoint,5 timeout. Unknown ownership or
hardware status is NA. Timeout means the required completion was not observed, not proof of no hardware execution.

PUBLISH_AUDIT/CQ_AUDIT/CQ_SOURCE remain bounded raw samples. CQ_SOURCE is not a full completion count and sequence
is not a request number. PUBLISH_AUDIT CqTail now comes from the unified CompletionSet cursor; the original private
Channel CQ tails are neither consumed nor repurposed. On fault, acknowledge only the already-accounted safe prefix
and stop. Unknown IDs are never discarded. Measurement=DiagnosticOnly; no target CANN/NPU or SIMD-performance
claim before returned evidence. Local tests in tests/ut/aicore/hcomm/test_simt_completion_host.py compile the actual
core and test skewed/independent windows, wrap, faults, capacity and realrun.sh output with mock processes;
they do not model hardware cache/DMA behavior.
