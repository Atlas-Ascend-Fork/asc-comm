# Hcomm SIMT URMA Performance Sample

Measures the issue performance of five point-to-point interfaces on the SIMT path: `WriteNbi`,
`WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`. Rank 0 sends and times; rank 1
only holds registered memory and verifies what landed.

Every case is driven by a single lane: one channel must be driven by one lane.

## Only the First Two Ranks Participate

This is a point-to-point benchmark: `<nranks>` may be >= 2, but only rank 0 (sender) and rank 1
(receiver) build the channel and run the benchmark. Every other rank prints `SKIP` and exits without
joining the communicator. Keeping a single sender is deliberate — concurrent senders contend for the
link and for SQ space, and the resulting latency and bandwidth numbers are no longer comparable.

For multi-card send/receive validation use [`simt_urma`](../simt_urma/README_en.md), which is a ring
topology in which every rank participates.

## What Is Measured

Five interfaces x two publication strategies:

| `api` | Description |
| --- | --- |
| `write` | `WriteNbi`; the WQE points at a local buffer through an SGE, payload length is configurable |
| `write_value` | `WriteValueNbi`; an 8-byte payload carried inline in the WQE, the local buffer is never read |
| `notify` | `WriteWithNotifyNbi`; writes a remote signal word after the payload, payload length is configurable |
| `faa` | `AtomicFAA`; accumulates into a single remote 8-byte word |
| `cas` | `AtomicCAS`; compare-and-swap on a single remote 8-byte word |

| `commit-mode` | Description |
| --- | --- |
| `immediate` | Every WQE uses `commit=true` and rings its own doorbell |
| `last` | All but the last use `commit=false`; the final post publishes the whole batch |

The two strategies differ in SQ footprint as well as doorbell count: a deferred `WriteNbi` occupies
one basic block, while a committed one splits its payload across two SGEs to fill the 128-byte DWQE
window and occupies two. The other four interfaces are fixed 2-BB WQEs either way. `SqBlocksPerWqe`
in the log reports the actual footprint.

Consecutive WQEs rotate over several slots so they do not all land on the same cache line — otherwise
the measurement reflects the remote memory system rather than the issue path. The two atomics are the
exception: they must target one word for the accumulation to mean anything, so they do not rotate.

## Timing Flow

```text
run the interface warmup times
Drain                         not timed

begin = clock()
run the interface iterations times
issueEnd = clock()
Drain
completionEnd = clock()
```

`IssueTime` and `AverageIssue` cover only the timed WQE submissions and are used to compare issue
latency between interfaces. `CompletionTime` runs from the start of the timed submissions to the end
of `Drain`, and is used only to compute `CompletionBandwidth`. `DrainStatus` and `Completed` confirm
the run actually completed correctly.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_urma_perftest/build.sh
```

## Usage

The communicator is created with `HcclGetRootInfo` and `HcclCommInitRootInfo`, so no rank table is
required. `run.sh` creates a temporary root info file for each round, written by rank 0 and read by
the other ranks.

```bash
# single point
examples/simt_urma_perftest/run.sh 2 write

# deferred publication
examples/simt_urma_perftest/run.sh 2 write --commit-mode last

# payload sweep: 8 16 64 256 1024 4096 16384 65536
examples/simt_urma_perftest/run.sh 2 write --sweep

# inline write
examples/simt_urma_perftest/run.sh 2 write_value

# Notify and the atomics
examples/simt_urma_perftest/run.sh 2 notify --payload-bytes 4096
examples/simt_urma_perftest/run.sh 2 faa
examples/simt_urma_perftest/run.sh 2 cas
```

Options: `--iterations` (default 1024), `--warmup` (default 100), `--payload-bytes` (default 4096),
`--commit-mode` (default immediate), `--sweep`, `--log`.

`--payload-bytes` only applies to `write` and `notify`; the other three interfaces always move 8
bytes and ignore it. Using `--sweep` with those three is rejected outright rather than running the
same 8-byte case eight times over.

`--sweep` starts a fresh round of processes for each payload, each with its own sync directory and
root info file — otherwise the previous round's barrier markers would let the next round pass
immediately, and the previous round's root info would be misread by the next. That directory is
passed to the executable as a command-line argument rather than an environment variable: an exported
variable is easy to lose across a shell wrapper or a scheduler, and once lost every round silently
shares the same default directory.

## Output

```text
RESULT | Path=SIMT-t1 | API=write | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | SqBlocksPerWqe=2 | Slots=64 | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
[rank 1] simt_urma_perftest write PASS
```

## Constraints

**`--payload-bytes` must be at least 2.** A committed `WriteNbi` splits its payload into
`firstLen = len >> 1` and `lastLen = len - firstLen`; a single byte would give the first segment a
zero length, which URMA does not accept.

**Only `write` and `write_value` are verified on the receiving side.** The `notify` signal word shares
a slot with its payload, and the two atomics rewrite their target word on every iteration, so the
landed image depends on the iteration count and there is no stable expected value to compare against.
Those cases rely on `DrainStatus` and `Completed` to determine whether the run completed correctly.

## Runtime Constraints

- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs on the same host are required to run; single-NPU environments only support
  compilation verification.
- The build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library.
