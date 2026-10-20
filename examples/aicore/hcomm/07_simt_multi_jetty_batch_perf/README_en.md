# Hcomm SIMT Multi-Jetty Parallel Submission Performance Sample

## Overview

This sample demonstrates how, in an Ascend C SIMT Kernel and based on the batch submission APIs of
`AscendC::simt::Hcomm`, multiple Jettys publish WQEs in parallel while one consumer uniformly handles the
shared CQ and advances the completion state of each Jetty by JFS ownership. It supports the `WriteNbi`,
`ReadNbi`, and `WriteWithNotifyNbi` APIs and measures the per-request device-side issue cost of multi-Jetty
parallel submission.

This case relies on Hcomm internal completion-management interfaces and is for engineering acceptance, not a
user API call example. `MakeBatchHandle` and every batch Write/Read/Notify explicitly pass a group; the same
handle uses a fixed Group and a fixed UB workspace; the publication flow includes three barriers, and the
shared CQ is uniformly managed by one consumer. Before running, install the run package that matches this
repository's source code and recompile the sample.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
multi_jetty_batch_perf
├── CMakeLists.txt                 // CMake build file
├── main.cpp                       // Host-side communication domain setup, memory registration, case scheduling, and validation
├── host_setup.h                   // Jetty channel creation and remote-memory index resolution
├── simt_kernel.cpp                // Single parallel VF with warmup/timing/Drain flow
├── simt_submit.h                  // Three-barrier publication sequence and per-lane SQ maintenance
├── simt_completion.h              // CompletionSet init/continuation, window registration, and completion snapshot
├── validation.h                   // Completion counts, all payload bytes, and Notify value validation
├── multi_jetty_batch_perf_common.h // Shared constants and host/device data layout
├── README.md                      // Chinese sample documentation
└── README_en.md                   // English sample documentation
```

## Sample Description

### Functionality Description

| API | Data Flow | Semantic Description |
| --- | --- | --- |
| `WriteNbi` | Local GM → Remote GM | Writes local Global Memory data to a specified address on the remote NPU. |
| `ReadNbi` | Remote GM → Local GM | Reads data from a specified address on the remote NPU into local Global Memory. |
| `WriteWithNotifyNbi` | Local GM → Remote GM | Writes the payload and then a remote notify word. |

MultiJetty submission mode:

- `--jetty-counts` specifies the total Jetty count of the rank pair; the lane count per Jetty equals the
  batch size and every lane builds one request, published as a whole batch by the last publisher lane, so the
  request count per batch also equals the lane count per Jetty. The lane count and the per-lane request count
  cannot be configured independently.
- Every lane maintains the SQ it constructs, and publishers of multiple Jettys publish concurrently. Three
  block barriers coordinate before publication, after publication, and at the start of the next batch; groups
  wait at these boundaries. The shared-CQ single-consumer rule only constrains completion management and does
  not require Jettys to submit in turn.
- The Jetty count that fits in a single kernel is determined by the compile-time batch size
  (`kMaxActiveLanes` / batch size, rounded down); when the Jetty count exceeds it, the launch is split into
  multiple kernels in the same stream, where the second kernel continues the same CompletionSet without
  clearing the CQ cursor. The output uses `Jettys` and `ConcurrentJettys` to distinguish the configured count
  from the actual maximum parallel count.

Data validation policy:

- Each case requires the completion count to PASS (`COMPLETION_CHECK`) and the corresponding end's data
  validation to pass; Notify additionally requires all notification values to pass. Read checks the initiator,
  and Write/Notify check the receiver, including warmup data.
- The sender and receiver fill identity patterns per jetty/operation, and every payload byte and notify word
  is validated.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Two-rank point-to-point communication (configurable Jetty count, parallel publication over multiple channels) |
| Invocation Method | Multi-Jetty parallel submission through APIs of `AscendC::simt::Hcomm` in a SIMT Kernel |
| Tested APIs | `write`, `read`, `notify`; `all` runs the three APIs in turn |
| Data Geometry | Fixed 64B payload, 1024 timed requests per Jetty, warmup 96 aligned upward to the batch size |
| Process Launch | Executable directly launches, internally forks multiple ranks |
| Supported Rank Count | 2 (a fixed pair) |

### Implementation Flow

1. The executable directly launches 2 rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   ranks exchange root info through the TCP control channel specified on the command line
   (`examples/aicore/utils/rank_sync.h`) and create the communication domain. The parent process launches one
   round of rank processes per case.
2. Call `HcclCommMemReg` to register the local send/recv buffers with the communication domain; use
   `HcclRankGraphGetLayers` and `HcclRankGraphGetLinks` to obtain the link endpoint to the peer, call
   `HcclChannelAcquire` to create the P2P channels in batch (engine `COMM_ENGINE_AIV`, protocol
   `COMM_PROTOCOL_UBC_CTP`), and call `HcclChannelGetRemoteMems` to resolve the registration index of the
   remote recv buffer of each channel.
3. The Host pre-initializes identity patterns per jetty/operation, and the sender launches the kernels in
   order after a barrier, split by kernel geometry.
4. On the Kernel side, the group of each Jetty executes `Init()` and `MakeBatchHandle`, then publishes warmup
   and timed requests in the three-barrier order; the shared CQ is advanced by the single consumer, and
   `Drain(CompletionSet)` waits for completion; each Jetty leaves a completion snapshot for the Host check.
5. The sender reads back the timing and completion status and validates the completion counts; Read
   additionally validates the data landed locally; the receiver validates the landed Write/Notify data and
   notify values.
6. Validation results of both ends are gathered over the control channel by allgather; the sender prints the
   RESULT line of the pair, and the parent process prints the aggregate result.

## Compilation and Execution

Follow the steps below in the root directory of this sample to compile and run it. This sample only supports NPU execution mode.

- Configure environment variables

  The sample is compiled against the asc-comm headers installed under the CANN directory. If they are not
  installed, first generate and install the development run package from the repository root; see
  [`docs/en/compilation/build_and_test.md`](../../../../docs/en/compilation/build_and_test.md) for details:

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_9.3.0_linux-<arch>.run --full
  ```

  Then load the CANN environment variables:

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If not specified, it defaults to `/usr/local/Ascend`.

- Run the sample

  Execute the following commands in the sample directory. The executable internally forks all rank processes,
  so it can be run directly. Only one batch size of the parallel VF is compiled at a time; switching the batch
  size requires re-running the CMake configure step:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DMULTI_JETTY_MATRIX_BATCH=1 ..
  make -j
  cd .. && ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2
  ```

  Common usage examples:

  ```bash
  # Default: Write/Read/Notify x J1/2/4/8, 12 cases in total.
  ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2
  # Run the write API only.
  ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2 write
  # Specify the Jetty count list.
  ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2 write --jetty-counts 4,8
  # Repeat the commands above for 8, 32, 64, 128, and 256 by reconfiguring with -DMULTI_JETTY_MATRIX_BATCH=<N>.
  ```

- Launch parameter description

  ```bash
  ./build/multi_jetty_batch_perf <ip:port> <nranks> [write|read|notify|all] [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of rank processes to launch; fixed at 2 for this sample |
  | `<api>` | `all` | Communication API to test; `all` runs `write`/`read`/`notify` in turn |
  | `--jetty-counts` | `1,2,4,8` | Comma-separated Jetty counts; each value must be one of 1, 2, 4, 8 |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU architecture: `dav-3510` corresponds to Ascend 950PR&950DT products |
  | `MULTI_JETTY_MATRIX_BATCH` | `1`/`8`/`32`/`64`/`128`/`256` (default `256`) | Requests per batch, which is also the lane count per Jetty; only one batch size of the parallel VF is compiled at a time, and it must match the data geometry at runtime |

- Expected output

  After a successful multi-card run, the terminal outputs the following — one line per case, followed by the
  aggregate result:

  ```text
  RESULT | API=write | Mode=MultiJetty | DataSize/B=64 | BatchSize=256 | Jettys=8 | ConcurrentJettys=4 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
  RESULT | Example=multi_jetty_batch_perf | Status=PASS
  ```

  The primary metric `IssueTicksPerRequest` = total ticks of the submission timing interval / the total request
  count; smaller is better. Raw clock() count, not converted to ns. It includes construction/publication and
  submission-path synchronization and excludes the subsequent `Drain` wait; warmup is not counted, multiple
  kernels accumulate the submission interval and divide it by the total request count, and host launch gaps are
  excluded. Only compare PASS rows, and do not directly compute the speedup against SIMD data whose clock units
  are not aligned. A normal run also prints the `COMPLETION_STATE`, `COMPLETION_CHECK`, and
  `DATA_AUDIT_SUMMARY` summaries, with details expanded on validation failure.

## Notes

- Running the sample requires 2 NPUs; a single-card environment only supports compilation verification.
- The batch size is a compile-time constant (`MULTI_JETTY_MATRIX_BATCH`); the kernel selects the parallel VF
  geometry by that value. Switching the batch size requires re-running the CMake configure step and rebuilding.
- The build enables `--cce-res-usage`; the build log contains the VF resource report (stack/register usage),
  which can be used to check resource constraints.
- `IssueTicksPerRequest` measures the average issue cost, not the end-to-end latency of a single request;
  before the clock units are aligned, do not directly compute the speedup against archived SIMD data.
- Case combinations that were not executed cannot be counted as PASS: if any case is interrupted
  (Ctrl+C/SIGTERM), the aggregate result is INTERRUPTED.
