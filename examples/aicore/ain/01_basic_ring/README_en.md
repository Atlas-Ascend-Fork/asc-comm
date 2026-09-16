# AIN Basic Ring Sample

## Overview

This sample demonstrates how to use basic AIN communication capabilities from an AscendC AIV Kernel through the `ain/ain.h` interface. The sample runs symmetrically across multiple ranks: each rank issues `Put` to the next rank, issues `Get` to the previous rank, and finally synchronizes through `AinBarrierSession`.

The Host side allocates memory, initializes the HCCL communicator, creates the HCCL Team, registers symmetric windows, and creates AIV communication channels. The Kernel side only submits data-plane communication tasks through the AIN interface.

## Supported Scope

| Item | Description |
| --- | --- |
| Product | `Ascend 950PR / Ascend 950DT` |
| NPU architecture | `dav-3510` |
| Communication protocol | `COMM_PROTOCOL_UB_CTP` |
| Communication engine | `COMM_ENGINE_AIV` |
| Deployment mode | Single-node multi-card |
| CANN package | The CANN package selected by the current `set_env.sh` environment |

> A single-card environment only supports build verification. Running this sample requires at least two NPUs.

## Directory Structure

```text
basic_ring
├── CMakeLists.txt          // Build project file
├── README.md               // Chinese README
├── README_en.md            // English README
└── basic_ring.asc          // Host-side setup, AICore-side AIN calls, and shared definitions
```

## Sample Flow

### Host-Side Resource Setup

1. Call `aclInit`, `aclrtSetDevice`, and `aclrtCreateStream` to initialize the runtime environment.
2. Create the multi-rank HCCL communicator through the `RankSyncContext` helper (which exchanges root info over a TCP control channel) and `examples::InitCommByRootInfo`.
3. Initialize a team descriptor via `HcclTeamCreateDescInit` (including `barrierCount`, `rankIds`, engine `COMM_ENGINE_AIV`, protocol `COMM_PROTOCOL_UB_CTP`, and `channelCnt`), then call `HcclTeamCreate` to create the team with AIV + UB_CTP communication channels.
4. Register `sendBuf` and `recvBuf` as symmetric windows through `HcclCommSymWinRegister`.
5. Pass `team`, `sendWin`, and `recvWin` to the Kernel.

### Kernel-Side AIN Communication

The Kernel side performs the following steps:

1. Construct an `AscendC::Ain` object and initialize the Hcomm UB temporary workspace.
2. The current rank uses `Put` to write its rank id to `recvWindow[rankId]` on the next rank.
3. The current rank uses `Get` to read `sendWindow[0]` from the previous rank into local `recvWindow[rankNum + rankId]`.
4. Call `Flush` to wait for AIN communication tasks to complete.
5. Call `AinBarrierSession::Sync` to synchronize ranks.

## Build

Run the following commands in this sample directory:

```bash
# If CANN is installed in the default path, for example as root.
# For a non-root user, replace /usr/local with ${HOME}.
source /usr/local/Ascend/cann/set_env.sh
# If CANN is installed in a custom path:
# source ${install_path}/cann/set_env.sh

rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## Run

After the build completes, run the following command in the sample directory:

```bash
./build/basic_ring_demo tcp://127.0.0.1:29622 <nranks>
```

For example, run with two cards:

```bash
./build/basic_ring_demo tcp://127.0.0.1:29622 2
```

The executable forks all rank processes internally and uses a TCP control channel to exchange the HCCL root info.

## Expected Result

Each rank reads back `recvBuf` and prints the `Put` and `Get` results, for example:

```text
[rank 0] basic_ring | PUT recvBuf[1]=1, GET recvBuf[2]=1 | PASS
[rank 1] basic_ring | PUT recvBuf[0]=0, GET recvBuf[3]=0 | PASS
RESULT | Example=basic_ring | Status=PASS
```

The sample succeeds when all ranks print `PASS` and the final status is `PASS`.

## Build Option

| Option | Default | Description |
| --- | --- | --- |
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | This sample only supports `dav-3510`, which corresponds to Ascend 950PR / Ascend 950DT. |

## Notes

- Load the CANN environment variables before building and running to ensure `ASCEND_CANN_PACKAGE_PATH`, ASC CMake modules, and runtime shared libraries are available.
- This sample depends on CANN `hccl`, `hcomm`, `ascendcl`, and `runtime` libraries.
- The number of ranks must not exceed the number of available NPUs.
- This sample supports only single-node multi-card execution and does not support multi-node execution.
