# AIN Basic Ring Sample

## Overview

This sample demonstrates how to use basic AIN communication capabilities from an AscendC AIV Kernel through the `ain/ain.h` interface. The sample runs symmetrically across multiple ranks: each rank issues `Put` to the next rank, issues `Get` to the previous rank, and finally synchronizes through `AinBarrierSession`.

The Host side allocates memory, initializes the HCCL communicator, creates the HCCL Team, registers symmetric windows, and creates AIV communication channels. The Kernel side only submits data-plane communication tasks through the AIN interface.

## Supported Scope

| Item | Description |
| --- | --- |
| Product | `Ascend 950PR / Ascend 950DT` |
| NPU architecture | `dav-3510` |
| Communication protocol | `COMM_PROTOCOL_UBC_CTP` |
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
├── basic_ring.asc          // Host-side setup, AICore-side AIN calls, and shared definitions
└── run.sh                  // Multi-rank launch script
```

## Sample Flow

### Host-Side Resource Setup

1. Call `aclInit`, `aclrtSetDevice`, and `aclrtCreateStream` to initialize the runtime environment.
2. Create the multi-rank HCCL communicator through `HcclGetRootInfo` and `HcclCommInitRootInfo`.
3. Configure `barrierCount` and call `HcclWorldTeamCreate` to create the world team.
4. Register `sendBuf` and `recvBuf` as symmetric windows through `HcclTeamWindowRegister`.
5. Call `HcclTeamChannelsCreate` to create AIV + UBC_CTP communication channels.
6. Pass `worldTeam`, `sendWin`, and `recvWin` to the Kernel.

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
./run.sh <nranks>
```

For example, run with two cards:

```bash
./run.sh 2
```

`run.sh` starts one `basic_ring_demo` process for each rank and uses a temporary file to exchange the HCCL root info. The default script timeout is 300 seconds.

## Expected Result

Each rank reads back `recvBuf` and prints the `Put` and `Get` results, for example:

```text
PUT RES rank 0 recvBuf[1]=1 expect=1
GET RES rank 0 recvBuf[2]=1 expect=1
rank 0 test pass
```

The sample succeeds when all ranks print `test pass`.

## Build Option

| Option | Default | Description |
| --- | --- | --- |
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | This sample only supports `dav-3510`, which corresponds to Ascend 950PR / Ascend 950DT. |

## Notes

- Load the CANN environment variables before building and running to ensure `ASCEND_CANN_PACKAGE_PATH`, ASC CMake modules, and runtime shared libraries are available.
- This sample depends on CANN `hccl`, `hcomm`, `ascendcl`, and `runtime` libraries.
- The number of ranks must not exceed the number of available NPUs.
- This sample supports only single-node multi-card execution and does not support multi-node execution.
