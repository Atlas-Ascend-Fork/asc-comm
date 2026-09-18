# CCU Builtin AllReduce Sample

## Overview

This sample demonstrates how to prepare CCU communication resources and start a CCU Server through the MC2
builtin APIs, and then submit an AllReduce Client communication request from an AICore kernel through the HCCL
high-level API.

The sample creates one rank for each available NPU device. Each rank has 256 FP32 input elements initialized to
`rankId + 1`. On the host, `Mc2AcquireCcResCtx` obtains the CCU resource context and `Mc2CcKernelLaunch` starts
the CCU Server. On the AICore, the same `ccResCtx` is passed to `Hccl::InitV2`, and `Hccl::AllReduce` performs
a SUM operation. Every output element on every rank is the sum of the corresponding input elements from all ranks.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
03_allreduce
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, CCU resource setup, and AICore communication Client implementation
```

## Sample Description

### Functionality

Each rank has 256 FP32 input elements, and every element on rank `d` is initialized to `d + 1`. The host
configures the communication engine, data types, SUM reduction type, and fallback algorithm, and obtains the
communication resource context. The AICore `all_reduce_kernel` then calls `Hccl::AllReduce<true>` to add the
corresponding elements from all ranks and writes the result to each rank's `recvBuf`.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AllReduce<true>(SUM) (submit CCU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AllReduce in an HCCL communication domain |
| CCU Server launch | Host-side `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AllReduce<true>` |
| Reduce operation | `HCCL_REDUCE_SUM` |
| AICore kernel | `all_reduce_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Input size per rank | 256 FP32 elements |
| Output size per rank | 256 FP32 elements |
| Algorithm configuration | `CcuSchedAllReduceSoleMesh` |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICore stream, and allocate the input and receive buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `CCU_SCHED` communication engine, FP32
   source and destination data types, `HCCL_REDUCE_SUM`, and the `CcuSchedAllReduceSoleMesh` algorithm.
4. Call `Mc2AcquireCcResCtx` to obtain the CCU resource context `ccResCtx` and its size from the HCCL communication
   domain.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` to start the CCU Server.
   The `stream` parameter is reserved for the AICPU path and is currently ignored by the CCU path.
6. Launch the AICore kernel through `all_reduce_kernel<<<1, nullptr, streamAiv>>>`. Inside the kernel,
   `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on the host.
7. The AICore side calls `Hccl::AllReduce<true>` to submit a SUM communication Client request, and calls `Wait`
   and `SyncAll` to wait for completion.
8. Synchronize the AICore stream, copy `recvBuf` back to the host, and verify that every element equals
   `1 + 2 + ... + rankSize`.
9. Destroy the HCCL communication domain, stream, and device memory. `ccResCtx` is owned by the communication
   domain and does not need to be freed separately.

## Build and Run

Perform the following steps in the sample root directory. This sample supports NPU run mode only.

- Set Environment Variables

  Configure the development environment according to your CANN installation.

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN installation directory. The default installation directory is
  > `/usr/local/Ascend`.

- Run the Sample

  Run the following commands in the sample directory.

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./demo
  ```

- Build Options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode. This sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture for Ascend 950PR/Ascend 950DT |

- Expected Output

  In a two-device run, the input elements of rank `d` are initialized to `d + 1`, so every AllReduce output
  element is `1 + 2 = 3`. The output is similar to:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, recvBuf: [ 3 3 3 ... ]
  rankId: 0, expected: 3, validation: PASS
  rankId: 1, recvBuf: [ 3 3 3 ... ]
  rankId: 1, expected: 3, validation: PASS
  ```

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `all_reduce_kernel`; both paths must use the same resource context.
- The AICore communication request must be submitted after the CCU Server is started. `Hccl::AllReduce` is not a
  host-side direct AllReduce operation.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE` (8).
