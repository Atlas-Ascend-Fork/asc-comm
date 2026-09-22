# SIMT Jetty WRITE Sample

## Overview

This sample demonstrates how to use URMA Jetty point-to-point communication capabilities from an AscendC SIMT Kernel through the `jetty/hcomm_jetty_simt.h` interface. It covers the `thread / warp / group` cooperation modes, the dispatch mode where multiple warps build WQEs in parallel and the group commits them together, and the mixed mode where SIMT and SIMD share the publication.

The Kernel side resolves queue and remote metadata directly from the GM Jetty table through the `HcommPeer` and `HcommJetty` interfaces and submits data-plane communication tasks, with no static or dynamic UB workspace. The fixed SGE template parameter ranges from 2 to 12; SGEs with a zero address or length do not count toward the WRITE, valid SGEs are compacted, and unused WQEBBs are padded with NOPs.

The deferred path consists of `Write<..., false>`, `AdvanceSq`, and `PublishSq`: `AdvanceSq` advances the SQ head and marks the final WQE, while `PublishSq` writes the 128B DWQE window to publish the queue — both steps are required. The immediate path `Put` builds the WQE, writes the SQ, and publishes the DWQE within a single call. Concurrent `Write` calls on the same channel must belong to the same batch and are committed by the group leader after every participating warp finishes; a group synchronization must happen after all lanes construct `HcommJetty` and before any warp starts to `Write`.

The `mixed` mode uses a `__global__ __mix__` entry: the SIMT VF builds and submits the SQ contents, and the AIV body then calls the SIMD `RingDoorbell()` through the shared 80B Jetty metadata to publish the queue, preserving the SIMT/SIMD shared-SQ scenario. Mixed is a correctness scenario and does not participate in the device-timed modes of `perf.sh`.

## Supported Scope

| Item | Description |
| --- | --- |
| Product | `Ascend 950PR / Ascend 950DT` |
| NPU architecture | `dav-3510` |
| Communication protocol | `COMM_PROTOCOL_UB_CTP` |
| Execution model | SIMT VF (the mixed mode additionally contains an AIV body) |
| Deployment mode | Single-node multi-card |
| CANN package | The CANN package selected by the current `set_env.sh` environment |

> A single-card environment only supports build verification. Running this sample requires at least two NPUs.

## Directory Structure

```text
simt_jetty
├── CMakeLists.txt          // Build project file
├── README.md               // Chinese README
├── README_en.md            // English README
├── build.sh                // Build script
├── run.sh                  // Multi-rank correctness launch script
├── stress.sh               // Repeated same-channel stress script
├── perf.sh                 // Device-side timing performance script
├── main.cpp                // Host-side setup and result verification
├── op_kernel.cpp           // Kernel-side SIMT Jetty calls
└── simt_jetty_common.h     // Shared sample definitions
```

## Mode Description

| Mode | Description |
| --- | --- |
| `thread` | A single thread builds a 4BB WQE with 12 valid SGEs, published per 2-SGE segment; verifies the scalar path |
| `warp` | Warp-cooperative WQE construction; verifies fixed slot reservation with immediate and deferred submission |
| `warp-pad` | One valid SGE plus zero-length SGEs; verifies the `1BB WRITE + 1BB NOP` padding path |
| `group` | Multiple warps share the batch state; the group leader commits |
| `dispatch` | Each warp reserves one WQE; the block commits the actual count |
| `mixed` | The SIMT VF builds the SQ contents; the AIV side publishes through the SIMD `RingDoorbell()` |

## Build

Run the following commands in this sample directory:

```bash
# If CANN is installed in the default path, for example as root.
# For a non-root user, replace /usr/local with ${HOME}.
source /usr/local/Ascend/cann/set_env.sh
# If CANN is installed in a custom path
# source ${install_path}/cann/set_env.sh

bash build.sh
```

Without hardware, the stack and register usage of the SIMT VF can still be checked at build time:

```bash
CCE_RES_USAGE=1 bash build.sh
```

The default SGE count is 12 and can be changed by rebuilding with `SIMT_JETTY_SGE_NUM` (range 1 to 12):

```bash
cmake -DSIMT_JETTY_SGE_NUM=6 .. && make -j
```

## Run

After the build completes, run the following commands in this sample directory:

```bash
./run.sh <nranks> [all|thread|warp|warp-pad|group|dispatch|mixed] [options]
```

For example, to run every mode on two cards:

```bash
./run.sh 2
```

A single mode can also be run to avoid a failing case polluting the SQ/CQ state of subsequent cases:

```bash
./run.sh 2 warp
./run.sh 2 mixed
```

Common options: `--iterations N` sets the correctness rounds (default 1); `--warmup N` and `--timing` are for the performance mode.

## Stress Verification

`stress.sh` repeats on the same channel/SQ, and every round waits for the receiver to finish checking. After each check the receiver zeroes the target buffer so a later round that fails to submit cannot be masked by earlier results. Every mode runs 100 times by default:

```bash
./stress.sh 2
./stress.sh 2 warp-pad 1000
```

## Performance Test

`perf.sh` uses the device-side `clock()`. The timed loop calls `Write` to build the WQE and records `write_cycles` when `Write` returns, then performs the commit and records `commit_cycles` separately, and finally records the full completion time `completion_cycles` after `Drain` returns, so `average_write_us` excludes the commit time. The default is a warmup of 100 followed by 1000 timed iterations, with one kernel launch per warmup/timed sample; every round drains so the SQ tail keeps advancing. Host launch and synchronization time is not included in the device cycles above:

```bash
./perf.sh 2
./perf.sh 2 warp 10000 1000
```

The cycles in the results are converted to microseconds on the assumption of a 1 GHz device clock, matching the statistics of `simt_urma_perftest`.

## Expected Results

In correctness modes, rank 1 verifies every slot of the receive buffer, for example:

```text
[rank 1] simt_jetty mode 4 iteration 0 PASS
```

The sample succeeds when every requested mode prints `PASS`.

## Notes

- Load the CANN environment variables before building or running so that `ASCEND_CANN_PACKAGE_PATH`, the ASC CMake modules, and the runtime libraries are available.
- This sample depends on the `hccl`, `hcomm`, `ascendcl`, and `runtime` libraries in the CANN package.
- `nranks` must be at least 2; the sample only actually uses rank 0 and rank 1, and the remaining ranks exit directly.
- This sample only supports single-node multi-card execution and does not support multi-node execution.
