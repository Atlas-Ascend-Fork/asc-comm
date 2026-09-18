# CCU Builtin AllGather + Add Sample

## Overview

This sample demonstrates how to prepare CCU communication resources and start a CCU Server through the MC2
builtin APIs, then submit an AllGather Client communication request from an AICore vector kernel through the HCCL
high-level API, and finally perform Add computation on the AllGather result.

The sample creates one rank for each available NPU device. Each rank provides one FP32 input segment. On the host,
`Mc2AcquireCcResCtx` obtains the CCU resource context and `Mc2CcKernelLaunch` starts the CCU Server. On the AICore,
the same `ccResCtx` is passed to `Hccl::InitV2`, and `Hccl::AllGather` interacts with the CCU Server to complete the
communication before the Add computation.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
02_allgather_add
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, CCU resource setup, AICore communication Client, and Add kernel implementation
```

## Sample Description

### Functionality

Each rank input contains `sendCount` FP32 elements. The host configures the communication engine, data types, and
algorithm, and obtains the communication resource context. The AICore `vector_add` kernel then calls
`Hccl::AllGather` to gather all rank inputs into `recvBuf`, reads `recvBuf`, and writes the input plus 8 to
`computeBuf`.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AllGather (submit CCU Client request)
  -> recvBuf
  -> Add 8
  -> computeBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AllGather in an HCCL communication domain |
| CCU Server launch | Host-side `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AllGather<true>` |
| AICore computation | `vector_add<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Input size per rank | 256 FP32 elements |
| AllGather output size | `rankSize * 256` FP32 elements |
| Add output size | `rankSize * 256` FP32 elements |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICore stream, and allocate the input, receive, and compute buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `CCU_SCHED` communication engine, FP32 source
   and destination data types, and the `CcuSchedAllGatherSoleMesh` algorithm configuration.
4. Call `Mc2AcquireCcResCtx` to obtain the CCU resource context `ccResCtx` and its size from the HCCL communication
   domain.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` to start the CCU Server.
   CCU kernel resources are prepared during resource acquisition, and the launch stage submits the Server task
   using the information in `ccResCtx`.
   The `stream` parameter is reserved for the AICPU path and is currently ignored by the CCU path.
6. Launch the AICore kernel through `vector_add<<<1, nullptr, streamAiv>>>`. Inside the kernel,
   `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on the host.
7. The AICore side calls `Hccl::AllGather<true>` to submit the communication Client request, calls `Wait` and
   `SyncAll` to wait for completion, copies `recvBuf` to the UB, adds 8, and writes the result to `computeBuf`.
8. Synchronize the AICore stream, copy `computeBuf` back to the host, and print the result.
9. Destroy the HCCL communication domain, stream, and device memory. `ccResCtx` is owned by the communication domain
   and does not need to be freed separately.

## Build and Run

Perform the following steps in the sample root directory. This sample supports NPU run mode only.

- Set Environment Variables

  Configure the development environment according to your CANN installation. The sample sets `CCU_SCHED` through
  `Mc2SetCcCommEngine` and does not use the old direct CCU kernel launch parameters.

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

  In a two-device run, the input elements of rank `d` are initialized to `d + 1`. The output is similar to:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, computeBuf: [ 9 9 9 ... 10 10 10 ... ]
  rankId: 1, computeBuf: [ 9 9 9 ... 10 10 10 ... ]
  ```

  The sample succeeds when every rank completes AllGather and prints the AICore Add result.
  `AscendC::printf` and `AscendC::DumpTensor` in `vector_add` may also print communication results and
  intermediate computation data.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `vector_add` kernel.
- The AICore communication request must be submitted after the CCU Server is started. `Hccl::AllGather` is not a
  host-side direct AllGather operation.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE`.
