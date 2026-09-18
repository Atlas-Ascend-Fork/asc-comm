# Hcomm Jetty WRITE Sample

## Overview

This sample demonstrates how to use URMA Jetty point-to-point communication capabilities from an AscendC AIV Kernel through the `jetty/hcomm_jetty.h` interface. The sample runs symmetrically across multiple ranks: each rank issues one `HcommJetty::Write` and one `HcommJetty::WriteValue` to every other rank, and finally waits for completion through `HcommJetty::Drain`.

The Host side initializes the HCCL communicator, registers the communication buffer, creates UBC_CTP channels, and builds the device-side Jetty table. The Kernel side submits data-plane communication tasks directly through the `HcommPeer` and `HcommJetty` interfaces without per-transfer Host involvement.

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
hcomm_jetty_write
├── CMakeLists.txt          // Build project file
├── README.md               // Chinese README
├── README_en.md            // English README
├── hcomm_jetty_write.asc   // Host-side setup, AICore-side Jetty calls, and shared definitions
└── run.sh                  // Multi-rank launch script
```

## Sample Flow

### Host-Side Resource Setup

1. Call `aclInit`, `aclrtSetDevice`, and `aclrtCreateStream` to initialize the runtime environment.
2. Create the multi-rank HCCL communicator through `HcclGetRootInfo` and `HcclCommInitRootInfo`.
3. Build a `COMM_ENGINE_AIV` + `COMM_PROTOCOL_UBC_CTP` channel descriptor for every non-local peer through `HcclRankGraphGetLayers`, `HcclRankGraphGetLinks`, and `HcclChannelDescInit`.
4. Register the communication buffer and obtain remote memory information through `HcclChannelGetRemoteMems`, which is used to derive the externally supplied remote destination device address.
5. Call `HcclCreateChannels` to create the channels, write each channel handle into the device-side Jetty table, and pass the local buffer address, the remote destination address, and the remote buffer index to the Kernel.
6. Launch one Kernel, then synchronize through an HCCL barrier and read back the buffers for verification.

### Kernel-Side Jetty Communication

The Kernel side performs the following steps:

1. Iterate over all peer contexts and construct `HcommPeer` and `HcommJetty` objects with user-allocated UB temporary buffers.
2. Call `HcommJetty::Write` to write 256 bytes of data to the remote receive slot.
3. Call `HcommJetty::WriteValue` to write an identifier to an independent marker address.
4. Call `HcommJetty::Drain` to wait for all submitted WQEs to complete.

## Build

Run the following commands in this sample directory:

```bash
# If CANN is installed in the default path, for example as root.
# For a non-root user, replace /usr/local with ${HOME}.
source /usr/local/Ascend/cann/set_env.sh
# If CANN is installed in a custom path
# source ${install_path}/cann/set_env.sh

rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## Run

After the build completes, run the following commands in this sample directory:

```bash
./run.sh <nranks>
```

For example, to run on two cards:

```bash
./run.sh 2
```

`run.sh` starts one `hcomm_jetty_write` process per rank and exchanges HCCL root info through a temporary file. The default script timeout is 300 seconds.

## Expected Results

Each rank validates its data slot and marker, for example:

```text
rank 0/2: Jetty WRITE and WriteValue validation passed for 1 peers
```

The sample succeeds when every rank prints `Jetty WRITE and WriteValue validation passed`.

## Build Options

| Option | Default | Description |
| --- | --- | --- |
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | This sample only supports `dav-3510`, which corresponds to Ascend 950PR / Ascend 950DT. |

## Notes

- Load the CANN environment variables before building or running so that `ASCEND_CANN_PACKAGE_PATH`, the ASC CMake modules, and the runtime libraries are available.
- This sample depends on the `hccl`, `hcomm`, `ascendcl`, and `runtime` libraries in the CANN package.
- The number of ranks must not exceed the number of available NPUs.
- This sample only supports single-node multi-card execution and does not support multi-node execution.
