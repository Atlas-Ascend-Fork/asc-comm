# SIMT Jetty WRITE Sample

## Overview

This sample demonstrates how to use Jetty point-to-point communication capabilities from an AscendC SIMT Kernel through the `jetty/hcomm_jetty_simt.h` interface. It covers the `thread / warp / group` cooperation modes, the dispatch mode where multiple warps build WQEs in parallel and the group commits them together, and the mixed mode where SIMT and SIMD share the publication.

The Kernel side resolves queue and remote metadata directly from the GM Jetty table through the `HcommPeer` and `HcommJetty` interfaces and submits data-plane communication tasks, with no static or dynamic UB workspace. The fixed SGE template parameter ranges from 2 to 12; SGEs with a zero address or length do not count toward the WRITE, valid SGEs are compacted, and unused WQEBBs are padded with NOPs.

The deferred path consists of `Write<..., false>`, `AdvanceSq`, and `PublishSq`: `AdvanceSq` advances the SQ head and marks the final WQE, while `PublishSq` writes the 128B DWQE window to publish the queue — both steps are required. The immediate path `Put` builds the WQE, writes the SQ, and publishes the DWQE within a single call. Concurrent `Write` calls on the same channel must belong to the same batch and are committed by the group leader after every participating warp finishes; a group synchronization must happen after all lanes construct `HcommJetty` and before any warp starts to `Write`.

The `mixed` mode uses a `__global__ __mix__` entry: the SIMT VF builds and submits the SQ contents, and the AIV body then calls the SIMD `RingDoorbell()` through the shared 80B Jetty metadata to publish the queue, preserving the SIMT/SIMD shared-SQ scenario. Mixed is a correctness scenario and does not support device timing.

This is a point-to-point verification: `<nranks>` may be >= 2, but only rank 0 (sender) and rank 1 (receiver) establish channels and run; the remaining ranks print `SKIP` and exit without joining the communicator.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
02_simt_hcomm_jetty
├── CMakeLists.txt          // Build project file
├── simt_jetty.asc          // Host-side communicator, memory registration, and verification flow
├── op_kernel.cpp           // Kernel-side SIMT Jetty calls
├── simt_jetty_common.h     // Definitions shared by host and kernel
├── README.md               // Chinese README
└── README_en.md            // English README
```

## Sample Description

### Functions

The sample provides six test modes, all of which can be run sequentially through `all` (default):

| Mode | Description |
| --- | --- |
| `thread` | A single thread builds a 4BB WQE with 12 valid SGEs, published per 2-SGE segment; verifies the scalar path |
| `warp` | Warp-cooperative WQE construction; verifies fixed slot reservation with immediate and deferred submission |
| `warp-pad` | One valid SGE plus zero-length SGEs; verifies the `1BB WRITE + 1BB NOP` padding path |
| `group` | Multiple warps share the batch state; the group leader commits |
| `dispatch` | Each warp reserves one WQE; the block commits the actual count |
| `mixed` | The SIMT VF builds the SQ contents; the AIV side publishes through the SIMD `RingDoorbell()` |

**Correctness and stress verification**: in every round rank 0 launches the kernel once, and the receiver verifies all slots (slots outside the written range must stay zeroed) and then zeroes the target buffer before the next round. Therefore `--iterations N` is also the number of repeated stress rounds on the same channel/SQ, and a later round that fails to submit cannot be masked by earlier results.

**Performance test**: `--timing` uses the device-side `clock()`. The timed loop calls `Write` to build the WQE and records `write_cycles` when `Write` returns, then performs the commit and records `commit_cycles` separately, and finally records the full completion time `completion_cycles` after `Drain` returns, so `average_write_us` excludes the commit time. The warmup and all timed iterations run inside a single kernel; every round drains so the SQ tail keeps advancing. Host launch and synchronization time is not included in the device cycles above. The cycles in the results are converted to microseconds on the assumption of a 1 GHz device clock, matching the statistics of `simt_urma_perftest`.

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | Point-to-point SIMT Jetty: only rank 0 sends and rank 1 receives |
| Communication protocol | `COMM_PROTOCOL_UBC_CTP` |
| Invocation | Jetty interfaces called directly inside the SIMT Kernel |
| Test modes | `thread`, `warp`, `warp-pad`, `group`, `dispatch`, `mixed`, `all` (default) |
| Supported ranks | Any >= 2 (rank 2 and above do not participate) |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   rank 2 and above exit immediately, while rank 0 and rank 1 exchange root info over the TCP control channel
   specified on the command line (`examples/aicore/utils/rank_sync.h`) and create the communicator.
2. Rank 0 and rank 1 each register `sendBuf` and `recvBuf`, create a UBC_CTP channel, and write the channel
   handle into a device-side Jetty table for the kernel to resolve.
3. In correctness mode the kernel is launched round by round; each round goes through the `written`/`checked`
   barriers: the sender waits for its WQEs to land, and the receiver verifies and re-zeroes the buffer before the
   next round is released. In timing mode the warmup and all timed iterations run inside a single kernel.
4. The receiver verifies every slot of `recvBuf` against the expected values and outputs PASS/FAIL; the parent
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
  ./simt_jetty tcp://127.0.0.1:29628 2
  ```

  A single mode can also be run to avoid a failing case polluting the SQ/CQ state of subsequent cases (each mode uses its own process group and they do not affect each other):

  ```bash
  ./simt_jetty tcp://127.0.0.1:29628 2 warp
  ./simt_jetty tcp://127.0.0.1:29628 2 mixed
  ```

  Stress verification and performance test:

  ```bash
  # Repeat the check 100 times on the same channel/SQ
  ./simt_jetty tcp://127.0.0.1:29628 2 warp-pad --iterations 100
  # Device-side timing: 100 warmup rounds, 1000 timed rounds
  ./simt_jetty tcp://127.0.0.1:29628 2 warp --timing --warmup 100 --iterations 1000
  ```

- Launch parameter description

  ```bash
  ./simt_jetty <tcp://ip:port> <nranks> [all|thread|warp|warp-pad|group|dispatch|mixed] [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of rank processes to launch; must be an integer >= 2 (only rank 0/1 participate) |
  | `[mode]` | `all` | Test mode, see the sample description; `all` runs every mode in order and automatically excludes `mixed` in timing mode; an unrecognized mode is rejected instead of silently falling back |
  | `--iterations` | `1` | Correctness verification rounds per mode |
  | `--warmup` | `0` | Warmup rounds for timing mode; must be used together with `--timing` |
  | `--timing` | Disabled | Device-side timing mode: all iterations run inside one kernel and per-stage cycles are reported |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT products |
  | `SIMT_JETTY_SGE_NUM` | `12` | Compile-time SGE count for the warp/group/dispatch/mixed modes, range 1 to 12 |
  | `HCOMM_CCE_RES_USAGE` | `OFF` | When enabled, prints the SIMT VF stack and register usage at build time for resource checking without hardware |

- Expected output

  Taking the `warp` mode on two cards as an example:

  ```text
  [rank 1] simt_jetty warp iterations 1 PASS
  RESULT | Example=simt_jetty Mode=warp | Status=PASS
  ```

  Timing mode additionally reports the per-stage device cycles:

  ```text
  PERF | mode=warp | iterations=1000 | warmup=100 | write_cycles=... | average_write_cycles=... | average_write_us=... | commit_cycles=... | average_commit_cycles=... | average_commit_us=... | completion_cycles=... | average_completion_cycles=... | average_completion_us=... | completed=1000
  RESULT | Example=simt_jetty Mode=warp | Status=PASS
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
- The mixed mode does not support `--timing`; `--warmup` must be used together with `--timing`.
- The build relies on the CANN ASC CMake utilities and links against the CANN `hcomm` library.
