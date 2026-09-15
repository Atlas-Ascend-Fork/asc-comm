# CCU Builtin AlltoAll Sample

## Overview

This sample demonstrates how to prepare CCU communication resources and start a CCU Server through the MC2
builtin APIs, and then submit an AlltoAll Client communication request from an AICore kernel through the
HCCL high-level API.

The sample creates one rank for each available NPU device. Each rank sends 256 FP32 elements to every
destination rank. On the host, `Mc2AcquireCcResCtx` obtains the CCU resource context and
`Mc2CcKernelLaunch` starts the CCU Server. On the AICore, the same `ccResCtx` is passed to `Hccl::InitV2`,
and `Hccl::AlltoAll` performs the equal-size data exchange.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
05_all2all
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, CCU resource setup, and AICore communication Client implementation
```

## Sample Description

Each rank stores one contiguous data block for every destination rank, with 256 FP32 elements per block.
The block sent from rank `i` to rank `j` is initialized to `i * 1000 + j`. After AlltoAll, block `i`
received by rank `j` should contain `i * 1000 + j`, which is used to validate the exchange.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AlltoAll<true> (submit CCU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AlltoAll in an HCCL communication domain |
| CCU Server launch | Host-side `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AlltoAll<true>` |
| AICore kernel | `all_to_all_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Data sent to each destination rank | 256 FP32 elements |
| Input/output size per rank | `rankSize * 256` FP32 elements |
| Algorithm configuration | `CcuSchedAllToAllSoleMesh` |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICore stream, and allocate the input and output buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `CCU_SCHED` communication engine, FP32
   source and destination data types, and the `CcuSchedAllToAllSoleMesh` algorithm configuration.
4. Call `Mc2AcquireCcResCtx` to obtain the CCU resource context `ccResCtx` and its size.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` to start the
   CCU Server. The `stream` parameter is reserved for the AICPU path and is currently ignored by the CCU path.
6. Launch the AICore kernel through `all_to_all_kernel<<<1, nullptr, streamAiv>>>`. Inside the kernel,
   `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on the host.
7. The AICore side calls `Hccl::AlltoAll<true>` to submit the equal-size exchange, and calls `Wait` and
   `SyncAll` to wait for completion.
8. Synchronize the AICore stream, copy `recvBuf` back to the host, and validate every block by source rank.
9. Destroy the HCCL communication domain, stream, and device memory. `ccResCtx` is owned by the communication
   domain and does not need to be freed separately.

## Build and Run

Run the following commands in this sample directory. This sample supports NPU execution only.

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./demo
```

`${install_path}` is the CANN installation directory. The default is `/usr/local/Ascend`.

In a two-device run, rank `d` sends a block containing `d * 1000 + j` to destination rank `j`.
The received blocks are ordered by source rank:

```text
Found 2 NPU device(s) available
rankId: 0, recv blocks: [ 0 x 256] [1000 x 256]
rankId: 0, validation: PASS
rankId: 1, recv blocks: [ 1 x 256] [1001 x 256]
rankId: 1, validation: PASS
```

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `all_to_all_kernel`.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE` (8).
