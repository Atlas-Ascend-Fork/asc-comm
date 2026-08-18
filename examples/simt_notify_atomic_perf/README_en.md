# SIMT Notify/FAA/CAS Basic Performance Sample

This directory tests only the `Notify`, `FAA`, and `CAS` interfaces. Immediate mode supports only `--lanes 1`.
Last mode supports `--lanes 1/32/1024`: lanes concurrently post with `commit=false`, then one lane performs the only
`commit=true` post after synchronization.

Before timing begins, the performance kernel calls `Hcomm::Init(workspace, len)`. In the opaque UB workspace, Hcomm
allocates a 128-byte WQE slot and a 32-byte remote registration/token cache for each lane, plus a 128-byte read-only
channel/SQ post context shared by all lanes. The shared context is initialized lazily after the first operation obtains
the channel. These caches reduce repeated GM metadata parsing without changing Immediate submission semantics, and the
caller does not manage or bind the three internal regions separately.

## Timing Flow

All three interfaces use the same timing flow:

```text
Execute warmup operations
Drain                         not timed

begin = clock()
Execute iterations operations
issueEnd = clock()
Drain/PollCq
completionEnd = clock()
```

`IssueTime` and `AverageIssue` cover only formal WQE submission and are used to compare issue latency.
`CompletionTime` starts with formal submission and ends after `Drain/PollCq`; it is used only to calculate
`CompletionBandwidth`. `DrainStatus` and `Completed` confirm successful completion.

## Last-Commit Mode

With `--commit-mode last`, a group of WQEs performs one final publication. The first `iterations - 1` WQEs use
`commit=false` to write the ordinary SQ, and the final WQE uses `commit=true` to publish the complete batch through the
DWQE window. Timing then stops and `Drain()` is called. Last mode combines DWQE publication only; SQ space is still
reserved with one CAS per WQE.

With `--lanes 1`, deferred WQEs are written serially. With `--lanes 32/1024`, lanes write deferred WQEs concurrently.
In both cases, one lane performs the final `commit=true` post and `Drain`.

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 \
  --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 faa \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 cas \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last
```

Change `--lanes` to `32` or `1024` to exercise multi-lane Last mode. Multi-lane Immediate mode is unsupported.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_notify_atomic_perf/build.sh
```

## Run Each Interface Separately

The communicator is created with `HcclGetRootInfo` and `HcclCommInitRootInfo`, so no HCCL rank table is required. `run.sh` creates a temporary root info file for each run, written by rank 0 and read by the other ranks.

Notify:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 --lanes 1
```

FAA:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 faa \
  --iterations 1024 --warmup 100 --lanes 1
```

CAS:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 cas \
  --iterations 1024 --warmup 100 --lanes 1
```

Each command executes one interface and one data size; the script does not scan or combine scenarios internally.
Immediate mode requires `--lanes 1`; multiple lanes require `--commit-mode last`.

## Output

```text
RESULT | Path=SIMT-t1 | API=notify | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
STATUS | API=notify | Path=SIMT-t1 | CommitMode=immediate | PASS
```

- `IssueTime/us`: Total formal interface-call time, excluding `Drain()`.
- `AverageIssue/ns`: `IssueTime / WqeCount`.
- `CompletionTime/us`: Time from the start of formal submission through completion of `Drain/PollCq`.
- `CompletionBandwidth/GB/s`: `DataSize × WqeCount / CompletionTime`.
- `DrainStatus=0` and `Completed=WqeCount`: The sample completed successfully.

FAA/CAS use a fixed `DataSize/B` of 8. Notify uses the size selected through `--payload-bytes`.

## Summarize and Plot

The scripts include only samples where `DrainStatus=0` and `Completed=WqeCount`. Samples are aggregated separately by
`Path`, `API`, `CommitMode`, and `DataSize/B` so that Immediate and Last data are not mixed.

```bash
python3 examples/simt_notify_atomic_perf/summarize.py perf.log \
  --api notify --commit-mode all --all-sizes

python3 examples/simt_notify_atomic_perf/plot.py perf.log \
  --api notify --commit-mode all --metric latency -o notify_latency.svg

python3 examples/simt_notify_atomic_perf/plot.py perf.log \
  --api notify --commit-mode all --metric bandwidth -o notify_bandwidth.svg
```

The latency plot uses `AverageIssue/ns`, while the bandwidth plot uses `CompletionBandwidth/GB/s`. If `--data-size`
is omitted, the summary script selects 8B for FAA/CAS and 4096B for Notify.
