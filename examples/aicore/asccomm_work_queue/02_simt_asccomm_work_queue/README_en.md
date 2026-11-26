# SIMT Jetty WRITE Sample

## Overview

This sample demonstrates Jetty point-to-point communication from an AscendC SIMT Kernel through the `asccomm_work_queue/asccomm_work_queue_simt.h` interface. It keeps only warp-level WRITE calls: `write` uses the contiguous-source interface and `write-sge` uses the multi-SGE interface.

The Kernel side resolves queue and remote metadata directly from the GM Jetty table through the `asccomm_peer` and `asccomm_work_queue` interfaces and submits data-plane communication tasks. Both modes move the same compile-time SGE count of data, so `write` and `write-sge` can be compared directly.

Both modes build the WQE, write the SQ, and publish the DWQE within one `write` call. After `write` returns, the warp calls the cooperative `drain` interface to wait for completion.

This is a point-to-point verification: `<nranks>` may be >= 2, but only rank 0 (sender) and rank 1 (receiver) establish channels and run; the remaining ranks print `SKIP` and exit without joining the communicator.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
02_simt_asccomm_work_queue
├── CMakeLists.txt          // Build project file
├── simt_work_queue.asc          // Host-side communicator, memory registration, and verification flow
├── op_kernel.cpp           // Kernel-side SIMT Jetty calls
├── simt_work_queue_common.h     // Definitions shared by host and kernel
├── README.md               // Chinese README
└── README_en.md            // English README
```

## Sample Description

### Functions

The sample provides two warp-level test modes, both of which can be run sequentially through `all` (default):

| Mode | Description |
| --- | --- |
| `write` | A warp calls the contiguous-source WRITE interface |
| `write-sge` | A warp calls the multi-SGE WRITE interface; each SGE covers one slot |

**Correctness and stress verification**: in every round rank 0 launches the kernel once, and the receiver verifies all slots (slots outside the written range must stay zeroed) and then zeroes the target buffer before the next round. Therefore `--iterations N` is also the number of repeated stress rounds on the same channel/SQ, and a later round that fails to submit cannot be masked by earlier results.

**Performance test**: `--timing` uses the device-side `clock()`. The timed loop calls `write` to build the WQE and records `write_cycles` when `write` returns, then performs the commit and records `commit_cycles` separately, and finally records the full completion time `completion_cycles` after `drain` returns, so `average_write_us` excludes the commit time. The warmup and all timed iterations run inside a single kernel; every round drains so the SQ tail keeps advancing. Host launch and synchronization time is not included in the device cycles above. The cycles in the results are converted to microseconds on the assumption of a 1 GHz device clock, matching the statistics of `simt_urma_perftest`.

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | Point-to-point SIMT Jetty: only rank 0 sends and rank 1 receives |
| Communication protocol | `COMM_PROTOCOL_UBC_CTP` |
| Invocation | Jetty interfaces called directly inside the SIMT Kernel |
| Test modes | `write`, `write-sge`, `all` (default) |
| Supported ranks | Any >= 2 (rank 2 and above do not participate) |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   rank 2 and above exit immediately, while rank 0 and rank 1 exchange root info over the TCP control channel
   specified on the command line (`examples/aicore/utils/rank_sync.h`) and create the communicator.
2. Rank 0 and rank 1 each register `send_buf` and `recv_buf`, create a UBC_CTP channel, and write the channel
   handle into a device-side Jetty table for the kernel to resolve.
3. In correctness mode the kernel is launched round by round; each round goes through the `written`/`checked`
   barriers: the sender waits for its WQEs to land, and the receiver verifies and re-zeroes the buffer before the
   next round is released. In timing mode the warmup and all timed iterations run inside a single kernel.
4. The receiver verifies every slot of `recv_buf` against the expected values and outputs PASS/FAIL; the parent
   process aggregates the results of all modes.

## Compilation and Execution

Perform the following steps in the root directory of this sample to compile and run it. This sample supports NPU run mode only.

- Configure environment variables

  The sample builds against the asc-comm headers installed into the CANN directory. If they are not yet installed, first generate and install the development
  verification run package from the repository root. For parameter details, see [`docs/en/compilation/build_and_test.md`](../../../../docs/en/compilation/build_and_test.md):

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_9.3.0_linux-<arch>.run --full
  ```

  Then load the CANN environment variables:

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If no installation directory is specified, the default is `/usr/local/Ascend`.

- Run the sample

  Execute the following commands in the sample directory. The executable internally forks all rank processes, so it can be run directly:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./simt_work_queue tcp://127.0.0.1:29628 2
  ```

  A single mode can also be run to avoid a failing case polluting the SQ/CQ state of subsequent cases (each mode uses its own process group and they do not affect each other):

  ```bash
  ./simt_work_queue tcp://127.0.0.1:29628 2 write
  ./simt_work_queue tcp://127.0.0.1:29628 2 write-sge
  ```

  Stress verification and performance test:

  ```bash
  # Repeat the check 100 times on the same channel/SQ
  ./simt_work_queue tcp://127.0.0.1:29628 2 write-sge --iterations 100
  # Device-side timing: 100 warmup rounds, 1000 timed rounds
  ./simt_work_queue tcp://127.0.0.1:29628 2 write --timing --warmup 100 --iterations 1000
  ```

- Launch parameter description

  ```bash
  ./simt_work_queue <tcp://ip:port> <nranks> [all|write|write-sge] [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of rank processes to launch; must be an integer >= 2 (only rank 0/1 participate) |
  | `[mode]` | `all` | Test mode, see the sample description; `all` runs both write modes in order; an unrecognized mode is rejected instead of silently falling back |
  | `--iterations` | `1` | Correctness verification rounds per mode |
  | `--warmup` | `0` | Warmup rounds for timing mode; must be used together with `--timing` |
  | `--timing` | Disabled | Device-side timing mode: all iterations run inside one kernel and per-stage cycles are reported |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT products |
  | `SIMT_WORK_QUEUE_SGE_NUM` | `12` | Compile-time SGE count for the multi-SGE write mode; the contiguous write moves the same amount of data, range 1 to 12 |
  | `HCOMM_CCE_RES_USAGE` | `OFF` | When enabled, prints the SIMT VF stack and register usage at build time for resource checking without hardware |

- Expected output

  Taking the `write` mode on two cards as an example:

  ```text
  [rank 1] simt_work_queue write iterations 1 PASS
  RESULT | Example=simt_work_queue Mode=write | Status=PASS
  ```

  Timing mode additionally reports the per-stage device cycles:

  ```text
  PERF | mode=write | iterations=1000 | warmup=100 | write_cycles=... | average_write_cycles=... | average_write_us=... | commit_cycles=... | average_commit_cycles=... | average_commit_us=... | completion_cycles=... | average_completion_cycles=... | average_completion_us=... | completed=1000
  RESULT | Example=simt_work_queue Mode=write | Status=PASS
  ```

  The sample succeeds when every requested mode prints `PASS` and the final status is `Status=PASS`.

## Notes

- At least two NPUs are required to run the sample; a single-card environment only supports build verification.
- The sample only actually uses rank 0 and rank 1; the remaining ranks print `SKIP` and exit without joining the communicator.
- This sample only supports single-node multi-card execution and does not support multi-node execution.
- When the Kernel side resolves a buffer base address through `AscendC::simt::LocalBufferAddr`, the argument is an
  index into the channel buffer table, not the `memHandles` array index: local table index 0 is reserved for the
  CCL buffer and the first explicitly registered buffer uses index 1; the remote buffer index is looked up by tag
  through `HcclChannelGetRemoteMems`.
- `--warmup` must be used together with `--timing`.
- The build relies on the CANN ASC CMake utilities and links against the CANN `asccomm` library.
