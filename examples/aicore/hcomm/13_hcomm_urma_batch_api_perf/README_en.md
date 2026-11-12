# Hcomm URMA Batch API Performance Example

## Overview

This example measures the send performance of three point-to-point operations over the URMA
path (AIV-Kernel direct drive) using the batch APIs (`MakeBatchHandle` + `BatchCommit`):
`WriteNbi`, `ReadNbi` and `WriteWithNotifyNbi`, covering the latency and bandwidth metrics and
both BatchCommit placement strategies (inside or outside the latency window).

The two ranks send and receive symmetrically: both sides issue communication to the peer and
validate their local landing regions. This is intentional — concurrent bidirectional traffic
contends for link and SQ resources, so the numbers reflect a "dual-stream concurrent" basis and
are not directly comparable with a single-sender basis (see
[`05_simt_hcomm_urma_perftest`](../05_simt_hcomm_urma_perftest/README_en.md)).

The topology is selected with `--perf_type` (default `p2p`, the symmetric two-rank form above):

- `p2p`: only ranks 0/1 communicate; with `--ranks` greater than 2 the remaining ranks stay
  idle — they still join the communicator and answer collective tags, but register no memory,
  build no channel and launch no kernel.
- `ring`: a unidirectional ring 0→1→...→N-1→0. Each rank sends `write`/`notify` to the next
  rank and pulls `read` from the previous one; every rank has a sender and is written by its
  upstream neighbour, and validation still checks each landing region against its writer's
  pattern. A 2-rank `ring` degenerates to `p2p`.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series | >= CANN 9.3.0 |

## Directory Structure

```text
13_hcomm_urma_batch_api_perf
├── CMakeLists.txt               // Build script
├── urma_batch_api_perf.asc      // Host side: communicator, memory registration, per-case loop, timing output
├── urma_batch_api_perf_kernel.cpp  // Device side: URMA batch API calls and timing
├── urma_batch_api_perf_def.h    // Definitions shared by Host and Kernel
├── README.md                    // Chinese readme
└── README_en.md                 // English readme
```

## Example Description

### Functions

Three operations x two metrics x two publishing strategies:

| `api` | Description |
| --- | --- |
| `write` | `WriteNbi` (batch-handle overload), local source region -> peer write region |
| `read` | `ReadNbi` (batch-handle overload), local read region <- peer source region |
| `notify` | `WriteWithNotifyNbi` (batch-handle overload), local source region -> peer notify region, plus one remote flag word after the payload |

| `metric` | Description |
| --- | --- |
| `lat` | Latency: per-round segmented timing; the timed window contains only the issues (plus BatchCommit when `commit=true`), no Drain — pure submit-path latency; BatchCommit and Drain stay outside the window (one out-of-window Drain between batches, so the SQ restarts from scratch each round) |
| `bw` | Bandwidth: one window covers all rounds of issues + per-batch `BatchCommit` + one trailing Drain — end-to-end bandwidth |

| `commit` | Description |
| --- | --- |
| `true` | BatchCommit is **counted in** the latency window (default) |
| `false` | BatchCommit moves after each round's stop-watch — **excluded from** the latency window |

The bandwidth metric does not distinguish the two: BatchCommit is always inside the bandwidth
window.

### Loop Model

- **rounds (default 1)** = outer batch-round repetition count;
- **batchCap (default 1024)** = requests per batch, which **determines the address window size**;
- every batch round writes the **same address window**: addresses depend only on the in-batch
  index `0..batchCap-1` (`dst + i*len` / `src + i*len`; the notify flag is a fixed single slot
  that does not advance with the index), rounds do not advance addresses;
- the host validates only **one batch window** — every round writes the same data (the source
  window pattern is constant).

Data correctness: each request lands at a different offset by in-batch index, and the host
validates the whole batch window — performance and function in one pass.

CQE strategy: **the last request of each batch uses cqe=1** (Drain polls the accumulated CQE
count and SQ recycling depends on it); the rest use cqe=0 — no intermediate CQEs interfere.

No error checking happens inside the timed window: return codes are unconditionally overwritten
(zero comparisons, zero branches) and judged uniformly after the window. Timing values are
returned to the host through the kernel context; the host recomputes the mean and derived
columns from full-precision cycles:

- `AvgCycles = Cycles / (Rounds x BatchSize)`
- `SingleCoreTime/us = AvgCycles / 1000` (Ascend950 counter at 1 GHz)
- `Bandwidth/GB/s = DataSize / SingleCoreTime / 1000`

### Memory Layout

One **1GB** registered buffer is partitioned per case `(len, batchCap)`; every region is one
batch window:

| Offset | Region |
| --- | --- |
| 0 | source: local send source / peer `ReadNbi` read source (advances by in-batch index) |
| batchCap x len | write: peer `WriteNbi` landing region (request i writes `base + i*len`) |
| 2 x batchCap x len | read: local `ReadNbi` landing region |
| 3 x batchCap x len | notify: peer `WriteWithNotifyNbi` landing region |
| 4 x batchCap x len (8B aligned) | flag: single notify flag slot (fixed address, 8B only) |

Notes:

- **One batch window**: write/notify sources and destinations advance by in-batch index; the
  read landing region advances by index while its source stays at the first entry of the peer
  source window (repeatedly reading the same value). Rounds do not change addresses — every
  round writes the same data and the host validates one window. The notify flag is a fixed
  single slot with fixed value `300`.
- **Batch capacity caps** (violations abort instead of clamping — the window size is
  determined by batchCap, and clamping would desynchronize the host validation window):
  - VECOUT UB: batch staging buffer `batchCap x WQEBBs-per-request x 64B <= 192KB` budget,
    validated at kernel runtime (`WriteWithNotifyNbi` takes 2 WQEBBs, the others 1);
  - SQ: under the latency metric one out-of-window Drain between batches lets the SQ restart
    per batch, so it must hold one batch: `batchCap x WQEBBs-per-request < sqDepth`; the
    default 1024 stays under half of the default sqDepth ring (4096), and violations surface
    as post-window Post/BatchCommit return codes;
  - Registered memory: `4 x batchCap x len + 8 <= 1GB`, validated at kernel runtime.
- Initial values: the source region is filled with `100 + myRank` (int32 pattern); landing
  regions and the flag slot are zeroed.

### Per-Case Serial Execution: launch -> validate -> refill -> next case

```
for batch in {min, min*2, ..., max}:        # --batch-range, default only 1024
  for len in {2^minExp, ..., 2^maxExp}:     # --exponent-range, default 8B..128KB
    refill initial values (fill one batch window of source + zero landing regions)
    barrier                          # release only after both ranks have refilled
    launch this case's kernel -> sync
    barrier                          # wait until the peer has drained too
    validate this case's data (one batch window)
    joint end-of-case decision -> both ranks stop if either failed
                                  # later cases would overwrite the scene, and a
                                  # one-sided stop would deadlock the peer
```

A single kernel carries all dimensions (message length / rounds / batch capacity / API / metric /
publishing strategy are all runtime parameters).

### Host-Side Validation

Per case (always reading local memory, one batch window), only the selected APIs are validated:

- the whole `batchCap x len` write / read / notify regions equal `100 + peerRank`;
- the notify flag slot equals `300` (fixed value): the single slot is written concurrently by a
  batch of requests; WQE in-order completion constrains completion reporting only, not the
  landing order of remote flag writes (measured: with batch=1024 the value left in the slot is
  nondeterministic), so per-request values are not checkable; a constant is independent of
  completion order, and checking it confirms the notify path works.

Comparison granularity is 4B (int32).

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | Topology via `--perf_type`: `p2p` (two symmetric ranks, default) or `ring` (unidirectional ring) |
| Invocation | Batch APIs (`MakeBatchHandle` + `BatchCommit`) inside an AIV Kernel |
| APIs under test | `write`, `read`, `notify` |
| Metrics | `lat` (submit latency), `bw` (end-to-end bandwidth) |
| Publishing strategies | `true` (BatchCommit in latency window, default), `false` (excluded) |
| Rank count | 2..N (`ring`: all ranks active; `p2p`: only ranks 0/1 communicate, the rest idle) |

## Build and Run

Follow these steps in the example directory. NPU mode only.

- Environment setup

  Configure the environment variables according to the CANN development toolkit
  [installation method](../../../../docs/quick_start_en.md).

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN install directory; it defaults to `/usr/local/Ascend`.

- Running the example

  From the example directory (the executable forks all rank processes itself):

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd ..
  ./build/urma_batch_api_perf tcp://127.0.0.1:29627           # default: 2 ranks, p2p
  ./build/urma_batch_api_perf tcp://127.0.0.1:29627 --perf_type ring --ranks 4
  ```

- Command line

  ```bash
  ./urma_batch_api_perf <tcp://ip:port> [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<tcp://ip:port>` | none | Control-channel address that rank 0 listens on, used for root-info exchange and Host barriers; use distinct ports for parallel instances |
  | `--ranks` | `2` | Number of rank processes to fork (>= 2); extra ranks are idle under `p2p`, all active under `ring` |
  | `--perf_type` | `p2p` | Communication topology: `p2p` (ranks 0/1 symmetric) or `ring` (unidirectional ring, e.g. 4 ranks: 0→1→2→3→0) |
  | `--exponent-range` | `3 17` | Data-size exponent range (size = 2^exp, inclusive, e.g. 3 17 = 8B..128KB) |
  | `--exponent` | none | Single exponent (equivalent to `--exponent-range exp exp`) |
  | `--api` | `all` | Operations to measure: `all`, or a comma-separated subset of `write`/`read`/`notify` |
  | `--metric` | `all` | `all`, or a comma-separated subset of `lat`/`bw` |
  | `--commit` | `true` | Both modes fire BatchCommit per batch; the difference is only whether it counts into the latency window; bandwidth does not distinguish |
  | `--rounds` | `1` | Batch-round repetitions (warmup is fixed at 1 full batch round, not configurable) |
  | `--batch-range` | `1024 1024` | Requests per batch (x2 steps, inclusive; determines the address window size) |
  | `--batch` | none | Single batch size (equivalent to `--batch-range N N`; capped by kernel runtime checks) |

- Build options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; NPU only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT series |

- Expected output

  ```text
  PERF_DATA | Rank=0 | API=WriteNbi | DataSize/B=4096 | BatchSize=1024 | Commit=true | Topology=p2p | Metric=lat | Rounds=1 | Cycles=148480 | AvgCycles=145.0000 | SingleCoreTime/us=0.145000 | Bandwidth/GB/s=28.1375
  [rank 0] case=batch1024_4096B validation: PASS (checked 4096KB per region)
  RESULT | Example=urma_batch_api_perf | Status=PASS
  ```

  One `PERF_DATA` line per operation x metric combination; after all cases finish, rank 0 writes
  the summary to `perf_data.csv` (in the working directory, including a `topology` column
  recording the run's topology). A `PASS` `RESULT` line with all case validations `PASS` means
  the example ran successfully.

## Notes

- At least 2 NPUs are required; single-card environments support compile-only validation.
- Batch-capacity constraints: the VECOUT UB budget and the registered-memory window are
  validated at kernel runtime; SQ-depth violations surface as post-window Post/BatchCommit
  return codes. All violations abort without clamping — the window size is determined by
  batchCap, and clamping would desynchronize the host validation window.
- Default batch 1024: `WriteWithNotifyNbi` takes 2 WQEBBs per request, and 1024x2=2048 stays
  under half of the default sqDepth ring (4096).
- Device memory requirement: 1GB registered memory per rank (worst case batch=1024 x len=128KB
  is a ~512MB landing region).
- Building depends on the CANN ASC CMake capability and links against the CANN `hcomm` library.
