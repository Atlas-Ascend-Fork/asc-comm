# CCU Builtin ReduceScatter Sample

## Overview

This sample demonstrates how to prepare CCU communication resources and start a CCU Server through the MC2
builtin APIs, and then submit a ReduceScatter Client communication request from an AICore kernel through the
HCCL high-level API.

The sample creates one rank for each available NPU device. Each rank provides `rankSize * 256` FP32 input
elements. After SUM reduction and scatter, each rank receives 256 elements. On the host,
`Mc2AcquireCcResCtx` obtains the CCU resource context and `Mc2CcKernelLaunch` starts the CCU Server. On the
AICore, the same `ccResCtx` is passed to `Hccl::InitV2`, and `Hccl::ReduceScatter` completes the communication.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
04_reducescatter
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, CCU resource setup, and AICore communication Client implementation
```

## Sample Description

Each rank has `rankSize * 256` FP32 input elements, and every element on rank `d` is initialized to `d + 1`.
ReduceScatter performs a SUM reduction and scatters the result by rank, so each rank receives 256 elements.
The host configures the CCU communication engine, data types, SUM reduction type, and fallback algorithm.
The AICore side submits the communication request through `Hccl::ReduceScatter<true>`.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::ReduceScatter<true>(SUM) (submit CCU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU ReduceScatter in an HCCL communication domain |
| CCU Server launch | Host-side `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::ReduceScatter<true>` |
| Reduce operation | `HCCL_REDUCE_SUM` |
| AICore kernel | `reduce_scatter_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Input size per rank | `rankSize * 256` FP32 elements |
| Output size per rank | 256 FP32 elements |
| Algorithm configuration | `CcuSchedReduceScatterSoleMesh` |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICore stream, and allocate the input and output buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `CCU_SCHED` communication engine, FP32
   source and destination data types, `HCCL_REDUCE_SUM`, and the
   `CcuSchedReduceScatterSoleMesh` algorithm configuration.
4. Call `Mc2AcquireCcResCtx` to obtain the CCU resource context `ccResCtx` and its size.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` to start the
   CCU Server. The `stream` parameter is reserved for the AICPU path and is currently ignored by the CCU path.
6. Launch the AICore kernel through `reduce_scatter_kernel<<<1, nullptr, streamAiv>>>`. Inside the kernel,
   `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on the host.
7. The AICore side calls `Hccl::ReduceScatter<true>` to submit a SUM communication Client request, and calls
   `Wait` and `SyncAll` to wait for completion.
8. Synchronize the AICore stream, copy `recvBuf` back to the host, and verify that every element equals
   `1 + 2 + ... + rankSize`.
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

In a two-device run, every output element should be `1 + 2 = 3`:

```text
Found 2 NPU device(s) available
rankId: 0, recvBuf: [ 3 3 3 ... ]
rankId: 0, expected: 3, validation: PASS
rankId: 1, recvBuf: [ 3 3 3 ... ]
rankId: 1, expected: 3, validation: PASS
```

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `reduce_scatter_kernel`.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE` (8).
