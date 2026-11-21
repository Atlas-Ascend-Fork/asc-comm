# AIN and Blaze AllGatherMatmul Execution Flow

English | [简体中文](ain_blaze_agmm_flow.md)

## Overview

This guide describes the Host execution flow, AIV communication path, AIC computation path,
and their synchronization. See the [sample README](README_en.md) for build instructions,
arguments, and constraints. Source links refer to this sample directory.
The Mermaid diagrams retain function names but omit arguments, exception paths, and resource details.
Use a Markdown viewer with Mermaid support to display them.

## Inputs and Outputs

Each device holds its own A and B. AllGatherMatmul concatenates A from all ranks in source-rank
order and multiplies the result by the current rank's B. For rank 0 in a four-rank group:

```text
Local inputs:  A0[M,K], B0[K,N]
Remote inputs: A1[M,K], A2[M,K], A3[M,K]
Output C0[4*M,N]: [A0 @ B0; A1 @ B0; A2 @ B0; A3 @ B0]
```

B may differ between ranks. Each rank writes directly to the corresponding output rows;
no final reordering is required.

| Term | Definition | Responsibility |
| --- | --- | --- |
| Host | CPU side | Allocate resources, launch kernels, verify outputs, and collect timings |
| rank | Participant in the communication group | One process manages one device; rank need not equal device ID |
| AIV | Communication core | Read remote A through AIN Get |
| AIC | Matrix computation core | Execute Blaze BlockMmad |
| GM | Device memory | Store inputs, received data, and outputs |
| tile_m | Rows per communication round | Partition each remote A along M |

## Host Execution Flow

Start at `main()`. Each child process enters `run_rank()` to initialize, run, and release one rank.

```mermaid
flowchart TD
    A["main() → parse_options()<br/>Read ranks, shapes, and modes"]
    B["process_group::launch()<br/>Fork ranks and enter run_rank()"]
    C["run_rank() → rank_sync_context::make_listen / make_connect<br/>Establish Host control connections"]
    D["initialize()<br/>Select device, create HCCL communicator<br/>Allocate memory, register windows, create team"]
    E["make_tiling()<br/>Select cores, chunk rows, and round count"]
    F["run_rank(): select mode<br/>Default both runs serial then fused"]
    G["FillInputs()<br/>Generate and upload A and B with a new seed"]
    H["run_once() → launch()<br/>Wait for input readiness, launch, and wait for completion"]
    I["verify()<br/>Check remote A and matrix output"]
    J{"run_rank()<br/>More input verification iterations?"}
    K["trace_once(): fused with trace enabled only<br/>Extra diagnostic call outside performance samples"]
    L["run_rank() → repeated run_once()<br/>Warm up, then measure steady state"]
    M["verify() and run_rank() statistics<br/>Final check, median and P95"]
    N{"run_rank()<br/>Another mode?"}
    O["rank_resources::Close()<br/>Release rank resources"]
    P["main() → process_group::wait_all()<br/>Collect child status and report PASS or FAIL"]
    A --> B --> C --> D --> E --> F --> G --> H --> I --> J
    J -->|Yes| G
    J -->|No| K
    K --> L --> M --> N
    N -->|Yes| F
    N -->|No| O --> P
```

### Host Execution and Synchronization

1. `initialize()` prepares resources. `launch()` submits the device communication and computation.
2. `run_once()` is one execution boundary: a Host barrier precedes the start event, followed by
   `launch()`, the end event, stream synchronization, and timing collection across ranks.
   Synchronization also prevents a faster rank from changing A while peers still use it.
3. Verification with changed inputs precedes warmup and measurement. `trace_once()` is a separate
   diagnostic call. For each measured iteration, the maximum rank event duration is collected;
   median and P95 are calculated from those maxima.
4. Disabled tracing is skipped. The parent enters `wait_all()` after launching children;
   the last diagram node represents completion of that wait, not its initial invocation.

| Mode | Entry points | Behavior |
| --- | --- | --- |
| fused | `FusedKernel()` | Communication and computation overlap in one MIX kernel |
| serial | `GatherKernel()` → `MatmulKernel()` | Finish communication before computation on the same stream |
| hccl | `HcclAllgather()` → `MatmulKernel()` | Ordinary HCCL AllGather followed by computation |
| comm | `GatherKernel()` | Verify AIN communication only |
| matmul | `MatmulKernel()` | Verify Blaze with Host-filled gathered input |

`both` is not a separate kernel: `run_rank()` executes serial and fused in sequence.

## Fused Kernel Execution Flow

`launch()` starts `FusedKernel()`: AIV executes `gather()` while AIC executes `matmul()`.
Each participating device follows the same two paths. Solid edges show local execution order;
dashed edges show cross-core notification dependencies.

```mermaid
flowchart TD
    S["launch() → FusedKernel()<br/>One MIX kernel with paired AIV and AIC cores"]
    subgraph COMM["AIV communication: gather()"]
        V0["gather()<br/>Initialize AIN and per-core UB descriptors"]
        V1["gather(): round r<br/>For r ≥ 2, CrossCoreWaitFlag(free)<br/>Wait for round r-2 notification slot"]
        V2["gather_round() → ain::get()<br/>Read chunk r from remote peers<br/>Write to local gathered memory"]
        V3["gather_round()<br/>flush_async() obtains channel<br/>wait() completes this core's communication"]
        V4["gather()<br/>CrossCoreSetFlag / WaitFlag: local AIV barrier<br/>CrossCoreSetFlag(ready): notify paired AIC"]
        V5{"gather()<br/>Another round?"}
        V6["gather()<br/>CrossCoreWaitFlag(free)<br/>Drain the last one or two acknowledgements"]
        V0 --> V1 --> V2 --> V3 --> V4 --> V5
        V5 -->|Yes: increment r| V1
        V5 -->|No| V6
    end
    subgraph CALC["AIC computation: matmul()"]
        C0["matmul() → compute::init()<br/>Initialize Blaze parameters"]
        C1["compute::run(local=true)<br/>compute local A × local B first"]
        C2["matmul()<br/>CrossCoreWaitFlag(ready)<br/>Wait for remote data of round r"]
        C3["compute::run(local=false)<br/>Distribute peer and matrix tiles<br/>Call Blaze BlockMmad::operator()"]
        C4["matmul()<br/>CrossCoreSetFlag(free)<br/>Return notification slot in FIX pipeline order"]
        C5{"matmul()<br/>Another round?"}
        C6["matmul() → PipeBarrier(PIPE_ALL)<br/>Complete computation pipelines"]
        C0 --> C1 --> C2 --> C3 --> C4 --> C5
        C5 -->|Yes: increment r| C2
        C5 -->|No| C6
    end
    S --> V0
    S --> C0
    V4 -.->|ready: round r data is readable| C2
    C4 -.->|free: required to reuse slot in round r+2| V1
    C4 -.->|Remaining free notifications| V6
    V6 --> E["Both FusedKernel() paths complete<br/>aclrtSynchronizeStream() returns in run_once()"]
    C6 --> E
```

### Chunking and Cross-Core Synchronization

Each round reads the same row range from **all remote peers**, rather than one peer per round.
With four ranks, `M=1024`, and `tile_m=256`, rank 0 reads rows 0–255 of A1/A2/A3 in round 0,
then rows 256–511 in round 1, for four rounds in total. Different AIV cores own different peers;
a core may own multiple peers if there are fewer cores than peers.

Local computation needs no remote data. `compute::run(local=true)` can overlap the first Get round.
Subsequent remote computation consumes completed chunks while AIV fetches later chunks.

| Stage | AIV | AIC |
| --- | --- | --- |
| Start | `gather_round(0)` | `compute::run(local=true)` |
| Next | `gather_round(1)` | `compute::run(local=false)` for round 0 |
| Next | `gather_round(2)` | `compute::run(local=false)` for round 1 |
| Finish | Complete communication and drain free notifications | compute the last chunk and finish output writes |

These are dependencies, not equal-duration time slots. A slower path causes waits on the other path;
the two notification slots bound how far communication can advance.

- `ready` indicates that remote data for a round has arrived. Each AIV waits for its own channels,
  joins the local AIV barrier, then notifies its paired AIC.
- `free` allows reuse of the notification slot after AIC has processed the round in pipeline order.
  The first two rounds do not wait for free; round r ≥ 2 consumes the acknowledgement for round r-2.
- The alternating slots are **notifications, not two repeatedly overwritten GM buffers**.
  `gathered` holds the full `[P,M,K]` region; rounds write different rows.
- Cores without communication or computation tasks still participate in notification handoffs.

`compute::run()` flattens peer, M-tile, and N-tile indices into tasks, distributed across AIC cores
by core index and stride. Blaze `BlockMmad` computes each tile. B always belongs to the current rank,
and C is written directly to the source rank's output rows. Local computation reads the original A,
not the unused self slot in `gathered`.

## Recommended Source Reading Order

| Order | File and functions | Focus |
| --- | --- | --- |
| 1 | [main.asc](main.asc): `main()`, `run_rank()` | Entry point, per-rank processes, test loop |
| 2 | `initialize()` in the same file | A/B/C, receive memory, windows, and team |
| 3 | `run_once()`, `launch()` in the same file | Submission of one device invocation |
| 4 | [kernel.h](kernel.h): `FusedKernel()` | Dispatch to the AIV and AIC paths |
| 5 | `gather()`, `gather_round()` in the same file | Chunk reads, completion, ready/free |
| 6 | `matmul()`, `compute::run()` in the same file | Local-first computation and remote chunks |
| 7 | [tiling.h](tiling.h): `make_tiling()` | Chunk sizes and boundary conditions |
