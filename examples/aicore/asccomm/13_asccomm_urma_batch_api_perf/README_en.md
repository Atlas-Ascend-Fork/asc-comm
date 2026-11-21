# asccomm URMA Batch API Performance Example

## Overview

This example measures the send performance of three point-to-point operations over the URMA
path (AIV-Kernel direct drive) using the batch APIs (`asccomm_make_batch_handle` + `asccomm_batch_commit`):
`asccomm_write_nbi`, `asccomm_read_nbi` and `asccomm_write_with_notify_nbi`, covering the latency and bandwidth metrics and
both asccomm_batch_commit timing strategies (combined or split timing).

The two ranks send and receive symmetrically: both sides issue communication to the peer and
validate their local landing regions. This is intentional — concurrent bidirectional traffic
contends for link and SQ resources, so the numbers reflect a "dual-stream concurrent" basis and
are not directly comparable with a single-sender basis (see
[`05_simt_asccomm_urma_perftest`](../05_simt_asccomm_urma_perftest/README_en.md)).

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
13_asccomm_urma_batch_api_perf
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
| `write` | `asccomm_write_nbi` (batch-handle overload), local source region -> peer write region |
| `read` | `asccomm_read_nbi` (batch-handle overload), local read region <- peer source region |
| `notify` | `asccomm_write_with_notify_nbi` (batch-handle overload), local source region -> peer notify region, plus one remote flag word after the payload |

| `metric` | Description |
| --- | --- |
| `lat` | Latency: per-round segmented timing; the timed window contains only the issues (plus asccomm_batch_commit when `commit=combine`), no asccomm_drain — pure submit-path latency; under `split`, asccomm_batch_commit stays outside the Post window, and one out-of-window asccomm_drain between batches lets the SQ restart from scratch each round |
| `bw` | Bandwidth: one window covers all rounds; each round runs Post + `asccomm_batch_commit` + asccomm_drain — end-to-end bandwidth with SQ usage independent of rounds |

| `commit` | Description |
| --- | --- |
| `combine` | Post and asccomm_batch_commit share one latency window (default) |
| `split` | The Post window stops first; asccomm_batch_commit is timed in its own window and emits an extra `commit-lat` row |

The bandwidth metric does not distinguish the two: asccomm_batch_commit is always inside the bandwidth
window.

### Loop Model

- **rounds (default 1)** = outer batch-round repetition count;
- **batch_cap (default 1024)** is only the number of requests aggregated by one asccomm_batch_commit;
- it does not determine the address window. In-batch requests and rounds reuse one `len`
  window; the notify flag remains one fixed slot; the host validates only **one `len` window**
  — every round writes the same data.

Data correctness: all requests reuse the same landing window, which the host validates —
performance and function in one pass.

CQE strategy: **the last request of each batch uses cqe=1** (Drain polls the accumulated CQE
count and SQ recycling depends on it); the rest use cqe=0 — no intermediate CQEs interfere.

No error checking happens inside the timed window: return codes are unconditionally overwritten
(zero comparisons, zero branches) and judged uniformly after the window. Timing values are
returned to the host through the kernel context; the host recomputes the mean and derived
columns from full-precision cycles:

- `AvgCycles = Cycles / (Rounds x BatchSize)`
- `SingleCoreTime/us = AvgCycles / 1000` (Ascend950 counter at 1 GHz)
- `Bandwidth/GB/s = DataSize / SingleCoreTime / 1000`

Latency and bandwidth rows use the formulas above, with one request as one sample. When
`commit=split`, an extra `commit-lat` row is emitted: its sample is one `asccomm_batch_commit` call, so
`AvgCycles = Cycles / Rounds`. That row describes only the submission call, does not derive
bandwidth, and always reports 0. The Post/asccomm_batch_commit/asccomm_drain control flow is identical under
combine and split; only the timing-window boundary changes.

### Memory Layout

One **2GB** registered buffer is partitioned per case by `len`; every region is one `len` window:

| Offset | Region |
| --- | --- |
| 0 | source: local send source / peer `asccomm_read_nbi` read source |
| len | write: peer `asccomm_write_nbi` landing region |
| 2 x len | read: local `asccomm_read_nbi` landing region |
| 3 x len | notify: peer `asccomm_write_with_notify_nbi` landing region |
| 4 x len (8B aligned) | flag: single notify flag slot (fixed address, 8B only) |

Notes:

- **One len window**: in-batch requests and rounds reuse the same source/destination windows.
  The notify flag is a fixed single slot with fixed value `300`.
- **Batch capacity caps** (violations abort instead of clamping — the window size is
  determined by batch_cap, and clamping would desynchronize the host validation window):
  - VECOUT UB: batch staging buffer `batch_cap x WQEBBs-per-request x 64B < 256KiB`,
    validated at kernel runtime (`asccomm_write_with_notify_nbi` takes 2 WQEBBs, the others 1);
  - SQ: bandwidth drains immediately after each round and latency drains out of window between
    batches, so the SQ only needs one batch. The default sq_depth is 8192 entries x 4 WQEBBs
    = 32768 WQEBBs; the stricter UB cap (at most 4096 WQEBBs) already satisfies it;
  - Registered memory: `4 x len + 8 <= 2GB`, validated at kernel runtime.
- Initial values: the source region is filled with `100 + my_rank` (int32 pattern); landing
  regions and the flag slot are zeroed.

### Per-Case Serial Execution: launch -> validate -> refill -> next case

```
for batch in {min, min*2, ..., max}:        # --batch-range, default only 1024
  for len in {2^min_exp, ..., 2^max_exp}:     # --exponent-range, default 8B..128KB
    refill initial values (fill one len window of source + zero landing regions)
    barrier                          # release only after both ranks have refilled
    launch this case's kernel -> sync
    barrier                          # wait until the peer has drained too
    validate this case's data (one len window)
    joint end-of-case decision -> both ranks stop if either failed
                                  # later cases would overwrite the scene, and a
                                  # one-sided stop would deadlock the peer
```

A single kernel carries all dimensions (message length / rounds / batch capacity / API / metric /
publishing strategy are all runtime parameters).

### Host-Side Validation

Per case (always reading local memory, one len window), only the selected APIs are validated:

- the whole `len` write / read / notify regions equal `100 + peer_rank`;
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
| Invocation | Batch APIs (`asccomm_make_batch_handle` + `asccomm_batch_commit`) inside an AIV Kernel |
| APIs under test | `write`, `read`, `notify` |
| Metrics | `lat` (submit latency), `bw` (end-to-end bandwidth) |
| Publishing strategies | `combine` (Post and asccomm_batch_commit in one latency window, default), `split` (separate windows, extra `commit-lat` row) |
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
  | `--exponent-range` | `3 17` | Data-size exponent range (size = 2^exp, inclusive; supported 3..28, default 8B..128KB) |
  | `--exponent` | none | Single exponent (equivalent to `--exponent-range exp exp`) |
  | `--api` | `all` | Operations to measure: `all`, or a comma-separated subset of `write`/`read`/`notify` |
  | `--metric` | `all` | `all`, or a comma-separated subset of `lat`/`bw` |
  | `--commit` | `combine` | `combine` times Post+asccomm_batch_commit in one window; `split` times them separately and emits an extra `commit-lat` row; bandwidth does not distinguish |
  | `--rounds` | `1` | Batch-round repetitions (warmup is fixed at 50 full batch rounds, not configurable) |
  | `--batch-range` | `1024 1024` | Requests per batch (x2 steps, inclusive; aggregation count only, not address-window size) |
  | `--batch` | none | Single batch size (equivalent to `--batch-range N N`; capped by kernel runtime checks) |

- Build options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; NPU only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT series |

- Expected output

  ```text
  PERF_DATA | Rank=0 | API=asccomm_write_nbi | DataSize/B=4096 | BatchSize=1024 | Commit=combine | Topology=p2p | Metric=lat | Rounds=1 | Cycles=148480 | AvgCycles=145.0000 | SingleCoreTime/us=0.145000 | Bandwidth/GB/s=28.1375
  PERF_DATA | Rank=0 | API=asccomm_write_nbi | DataSize/B=4096 | BatchSize=1024 | Commit=split | Topology=p2p | Metric=commit-lat | Rounds=100 | Cycles=6500 | AvgCycles=65.0000 | SingleCoreTime/us=0.065000 | Bandwidth/GB/s=0
  [rank 0] case=batch1024_4096B validation: PASS (checked 4096KB per region)
  RESULT | Example=urma_batch_api_perf | Status=PASS
  ```

  One `PERF_DATA` line per operation x metric combination; with `commit=split` and latency
  selected, each operation emits one extra `commit-lat` line. After all cases finish, rank 0
  writes the summary to `perf_data.csv` (in the working directory, including a `topology`
  column recording the run's topology). A `PASS` `RESULT` line with all case validations
  `PASS` means the example ran successfully.

## Notes

- At least 2 NPUs are required; single-card environments support compile-only validation.
- Batch-capacity constraints: the VECOUT UB budget and the registered-memory window are
  validated at kernel runtime. All violations abort without clamping.
- Default batch 1024: `asccomm_write_with_notify_nbi` takes 2 WQEBBs per request, so staging is 128KiB,
  below the 256KiB UB budget and far below the 32768-WQEBB SQ capacity.
- Device memory requirement: 2GB registered memory per rank, supporting `len=256MB`.
- Building depends on the CANN ASC CMake capability and links against the CANN `asccomm` library.
