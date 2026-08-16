# SIMT Notify/FAA/CAS Basic Performance Sample

This directory tests only the `Notify`, `FAA`, and `CAS` interfaces. In Immediate mode, t1 uses `commit=true` for every
WQE. Multi-lane paths cooperatively reserve and publish WQEs in rounds and do not include intermediate timing points.

When up to 32 lanes call `commit=true` on the same channel, Hcomm combines the SQ reservation and DWQE publication for
the warp: one lane atomically reserves the SQ range, every lane fills its own WQE, and the final active lane publishes
the group. Because t1024 spans multiple warps, each group explicitly calls `ReserveDwqeBatch`: non-publishing lanes
write the SQ with `commit=false`, and lane 0 publishes the group after block synchronization.

Before timing begins, the performance kernel calls `Hcomm::Init(workspace, len)`. In the opaque UB workspace, Hcomm
allocates a 128-byte WQE slot and a 32-byte remote registration/token cache for each lane, plus a 128-byte read-only
channel/SQ post context shared by all lanes. The shared context is initialized lazily after the first operation obtains
the channel. These caches reduce repeated GM metadata parsing without changing Immediate submission semantics, and the
caller does not manage or bind the three internal regions separately.

t1024 provides only the 128 KiB needed for 1024 per-lane WQE slots so that no single static UB object exceeds 128 KiB.
The optional remote cache and shared post context are disabled, and Hcomm falls back to GM metadata parsing. The log
therefore reports `RemoteCache=Disabled` and `PostContext=GM` for t1024.

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
DWQE window. Timing then stops and `Drain()` is called.

- t1 calls `ReserveDwqeBurst` once, performs one GM head reservation for the whole batch, and reuses remote
  registration data, SQ context, and fixed WQE fields.
- t32/t1024 lanes fill the deferred SQ WQEs in parallel. After all lanes synchronize, the lane that owns the final
  logical operation performs the only `commit=true` call, while the other lanes wait for publication to finish.

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 \
  --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json faa \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json cas \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last
```

Set `--lanes` to `32` or `1024` to exercise the corresponding multi-lane Last path. The default remains
`--commit-mode immediate`, and the selected `CommitMode` is recorded in the log.

Lane `lane` processes WQE indices `lane, lane + laneCount, ...`. Hcomm transparently combines t32 as a single-warp
operation. t1024 cooperatively submits groups of up to 1024 WQEs. When warmup contains fewer than 1024 WQEs, inactive
lanes still participate in synchronization but do not occupy SQ space. After all lanes return, lane 0 calls `Drain()`
once.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_notify_atomic_perf/build.sh
```

## Run Each Interface Separately

Notify:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 --lanes 1
```

FAA:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json faa \
  --iterations 1024 --warmup 100 --lanes 1
```

CAS:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json cas \
  --iterations 1024 --warmup 100 --lanes 1
```

Set `--lanes` to `32` or `1024` to run the same interface with multiple lanes. Each command executes one interface,
one lane configuration, and one data size; the script does not scan or combine scenarios internally.

## Output

```text
RESULT | Path=SIMT-t1 | API=notify | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | RemoteCache=PerLaneUB | PostContext=SharedUB | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
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
