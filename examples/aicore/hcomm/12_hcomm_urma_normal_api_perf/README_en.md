# Hcomm URMA Normal API Performance Example

## Overview

This example measures the send performance of three point-to-point APIs over the URMA path
(AIV-Kernel direct drive): `WriteNbi`, `ReadNbi` and `WriteWithNotifyNbi`, each covering the
latency and bandwidth metrics and both immediate and deferred publishing strategies.

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
12_hcomm_urma_normal_api_perf
├── CMakeLists.txt               // Build script
├── urma_normal_api_perf.asc     // Host side: communicator, memory registration, per-case loop, timing output
├── urma_normal_api_perf_kernel.cpp  // Device side: URMA API calls and timing
├── urma_normal_api_perf_def.h   // Definitions shared by Host and Kernel
├── README.md                    // Chinese readme
└── README_en.md                 // English readme
```

## Example Description

### Functions

Three APIs x two metrics x two publishing strategies:

| `api` | Description |
| --- | --- |
| `write` | `WriteNbi`, local source region -> peer write region |
| `read` | `ReadNbi`, local read region <- peer source region |
| `notify` | `WriteWithNotifyNbi`, local source region -> peer notify region, plus one remote flag word after the payload |

| `metric` | Description |
| --- | --- |
| `lat` | Latency: the timed window contains only the issue loop, no Drain — pure submit-path latency (the final `Commit` of the `final` mode is also outside the window) |
| `bw` | Bandwidth: a single window covers the issue loop plus one trailing Drain — end-to-end bandwidth |

| `commit` | Description |
| --- | --- |
| `inline` | Template parameter `commit=true` on every call: each issue rings its own doorbell |
| `final` | Template parameter `commit=false` writes SQEs only; one `Commit` fires after each timed window — under `lat` the Commit is outside the window, under `bw` the trailing Commit + Drain are inside |

Timing flow: after `min(100, rounds)` warmup issues followed by a `Drain` (not timed), the
kernel issues `rounds` requests and records the in-window cycles. Under `lat` the window
contains only the issues (the `final`-mode Commit is outside); under `bw` the window also
covers the tail (`final`: trailing Commit + Drain; `inline`: trailing Drain).

No error checking happens inside the timed window: return codes are unconditionally overwritten
(zero comparisons, zero branches) and judged uniformly after the window, keeping the measured
path and the error path fully separated. Timing values are returned to the host through the
kernel context; the host recomputes the mean and derived columns from full-precision cycles:

- `AvgCycles = Cycles / Rounds`
- `SingleCoreTime/us = AvgCycles / 1000` (Ascend950 counter at 1 GHz)
- `Bandwidth/GB/s = DataSize / SingleCoreTime / 1000`

### Memory Layout

All cases share one **2GB** registered buffer, split by absolute offsets:

| Offset | Size | Region |
| --- | --- | --- |
| 0 | 256MB | source: local send source / peer `ReadNbi` read source |
| 256MB | 512MB | write: peer `WriteNbi` landing region (advances `dst + i*len` per round) |
| 768MB | 512MB | read: local `ReadNbi` landing region (advances per round) |
| 1280MB | 512MB | notify: peer `WriteWithNotifyNbi` landing region (advances per round) |
| 1792MB | 8B | flag: single notify flag slot (fixed address) |

Notes:

- **Fixed source, advancing destination**: the Write/Notify source stays in the local source
  region; the destination advances per round to avoid self-overwrite; the Read source stays in
  the peer source region and its local landing region advances.
- **Rounds cap**: each case requires `rounds * len <= 512MB` (landing-region capacity); the
  host validates every case up front and aborts on violation.
- Initial values: the source region is filled with `100 + myRank` (int32 pattern); landing
  regions and the flag slot are zeroed.

### Per-Case Serial Execution: launch -> validate -> refill -> next case

```
for len in {2^minExp, ..., 2^maxExp}:   # data size = 2^exp, exponent +1 per case, inclusive
    refill initial values (zero landing regions + refill source pattern)
    barrier                       # release only after both ranks have refilled
    launch this case's kernel -> sync
    barrier                       # wait until the peer has drained too
    validate this case's data
    joint end-of-case decision -> both ranks stop if either failed
                                  # later cases would overwrite the scene, and a
                                  # one-sided stop would deadlock the peer
```

A single kernel carries all dimensions (message length / API / metric / publishing strategy /
rounds are all runtime parameters).

### Host-Side Validation

No full 2GB host mirror: one 256MB staging buffer (`aclrtMallocHost`) uploads the source during
refill and copies landing regions back in chunks for comparison. Per case (always reading local
memory), only the selected APIs are validated:

- the source region still equals `100 + myRank` (untouched);
- the actually written `rounds*len` span of the write / read / notify regions equals
  `100 + peerRank`;
- the notify flag slot equals `300` (fixed value): the single slot is written concurrently by
  `rounds` requests; WQE in-order completion constrains completion reporting only, not the
  landing order of remote flag writes, so per-request values are not checkable; a constant is
  independent of completion order, and checking it confirms the notify path works (a total
  miss leaves the initial 0 in the slot, which is detectable).

Comparison granularity is 4B (int32).

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | Topology via `--perf_type`: `p2p` (two symmetric ranks, default) or `ring` (unidirectional ring) |
| Invocation | URMA APIs called directly inside an AIV Kernel |
| APIs under test | `write`, `read`, `notify` |
| Metrics | `lat` (submit latency), `bw` (end-to-end bandwidth) |
| Publishing strategies | `inline` (default), `final` |
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
  ./build/urma_normal_api_perf tcp://127.0.0.1:29626           # default: 2 ranks, p2p
  ./build/urma_normal_api_perf tcp://127.0.0.1:29626 --perf_type ring --ranks 4
  ```

- Command line

  ```bash
  ./urma_normal_api_perf <tcp://ip:port> [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<tcp://ip:port>` | none | Control-channel address that rank 0 listens on, used for root-info exchange and Host barriers; use distinct ports for parallel instances |
  | `--ranks` | `2` | Number of rank processes to fork (>= 2); extra ranks are idle under `p2p`, all active under `ring` |
  | `--perf_type` | `p2p` | Communication topology: `p2p` (ranks 0/1 symmetric) or `ring` (unidirectional ring, e.g. 4 ranks: 0→1→2→3→0) |
  | `--exponent-range` | `3 17` | Data-size exponent range (size = 2^exp, inclusive, e.g. 3 17 = 8B..128KB) |
  | `--exponent` | none | Single exponent (equivalent to `--exponent-range exp exp`) |
  | `--api` | `all` | APIs to measure: `all`, or a comma-separated subset of `write`/`read`/`notify` |
  | `--metric` | `all` | `all`, or a comma-separated subset of `lat`/`bw` |
  | `--commit` | `inline` | `inline` (commit=true per call) / `final` (commit=false + one Commit per timed window; Commit excluded from latency, included in bandwidth) |
  | `--rounds` | `1000` | Timed iterations per case (warmup is fixed at `min(100, rounds)`, not configurable) |

- Build options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; NPU only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT series |

- Expected output

  ```text
  PERF_DATA | Rank=0 | API=WriteNbi | DataSize/B=4096 | Commit=inline | Topology=p2p | Metric=lat | Rounds=1000 | Cycles=145000 | AvgCycles=145.0000 | SingleCoreTime/us=0.145000 | Bandwidth/GB/s=28.1375
  [rank 0] case=4096B validation: PASS (checked 3MB per region)
  RESULT | Example=urma_normal_api_perf | Status=PASS
  ```

  One `PERF_DATA` line per API x metric combination; after all cases finish, rank 0 writes the
  summary to `perf_data.csv` (in the working directory, including a `topology` column recording
  the run's topology). A `PASS` `RESULT` line with all case validations `PASS` means the
  example ran successfully.

## Notes

- At least 2 NPUs are required; single-card environments support compile-only validation.
- Landing-region capacity is 512MB per case: parameter combinations with `rounds * len` beyond
  that abort at startup (reduce `--rounds` or narrow `--exponent-range`).
- Under the latency metric no Drain happens inside the timed window, so `rounds` requests must
  all reside in the SQ; SQ-overflow shows up as a post-window Post return code
  (`WriteWithNotifyNbi` occupies 2 WQEBBs, the others 1).
- Building depends on the CANN ASC CMake capability and links against the CANN `hcomm` library.
