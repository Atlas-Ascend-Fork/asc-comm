# AICPU MC2 ReduceScatter Sample

## Overview

This sample demonstrates how to prepare AICPU communication resources and start an AICPU KFC Server
through the MC2 builtin APIs, and then submit a ReduceScatter Client communication request from an
AICore kernel through the HCCL high-level API.

The sample creates one rank for each available NPU device. Each rank provides `recvCount * rankSize`
FP32 input elements. On the host, `Mc2AcquireCcResCtx` obtains the communication resource context and
`Mc2CcKernelLaunch` starts the AICPU KFC Server on a dedicated stream. On the AICore, the same
`ccResCtx` is passed to `Hccl::InitV2`, and `Hccl::ReduceScatter` interacts with the AICPU KFC Server
to reduce the inputs of all ranks element-wise and scatter the result by rank id, so each rank receives
only its own block.

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
└── main.asc          // Host flow, AICPU resource setup, and AICore communication Client implementation
```

## Sample Description

### Functionality

Each rank input contains `sendCount = recvCount * rankSize` FP32 elements. The host configures the
`AICPU_TS` communication engine, the FP32 source/destination data types, and the SUM reduce type, and
obtains the communication resource context. The AICore `reduce_scatter_kernel` then calls
`Hccl::ReduceScatter` to reduce the inputs of all ranks element-wise and split the result in rank id
order, so the `recvBuf` of each rank keeps only its own block of `recvCount` elements.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server on aicpuStream)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::ReduceScatter (submit AICPU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | AICPU ReduceScatter in an HCCL communication domain |
| Communication engine | `AICPU_TS` (`OpExecuteConfig` value 2) |
| Reduce type | SUM (`Mc2SetCcReduceType` with `HCCL_REDUCE_SUM`) |
| AICPU Server launch | Host-side `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::ReduceScatter<true>` |
| AICore kernel | `reduce_scatter_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Input size per rank | `rankSize * 256` FP32 elements |
| Output size per rank | 256 FP32 elements |
| Supported ranks | No more than `MC2_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICPU and AICore streams, and allocate the input and
   receive buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `AICPU_TS` communication engine,
   the FP32 source and destination data types, and the SUM reduce type.
4. Call `Mc2AcquireCcResCtx` to obtain the communication resource context `ccResCtx` and its size from
   the HCCL communication domain.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` to
   start the KFC Server on the AICPU stream. The server blocks waiting for AICore Client messages, so it
   must not share the stream with the AICore kernel.
6. Launch the AICore kernel through `reduce_scatter_kernel<<<1, nullptr, streamAiv>>>`. Inside the
   kernel, `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on
   the host.
7. The AICore side calls `Hccl::ReduceScatter<true>` to submit the communication Client request, calls
   `Wait` and `SyncAll` to wait for completion, and finally calls `Finalize` to notify the server to exit.
8. Synchronize the AICore stream first and then the AICPU stream, copy `recvBuf` back to the host, and
   print and validate the result.
9. Destroy the HCCL communication domain, both streams, and device memory. `ccResCtx` is owned by the
   communication domain and does not need to be freed separately.

## Build and Run

Perform the following steps in the sample root directory. This sample supports NPU run mode only.

- Set Environment Variables

  Configure the development environment according to your CANN installation. The sample sets
  `AICPU_TS` through `Mc2SetCcCommEngine`.

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

  In a two-device run, the input elements of rank `d` are initialized to `d + 1`, and every element
  after the reduce sum is `devCount * (devCount + 1) / 2`. The output is similar to:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, recvBuf: [ 3 3 3 ... ]
  rankId: 1, recvBuf: [ 3 3 3 ... ]
  rankId: 0, expected: 3, validation: PASS
  rankId: 1, expected: 3, validation: PASS
  ReduceScatter sample pass
  ```

  The sample succeeds when every rank completes ReduceScatter and passes validation.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support
  compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `reduce_scatter_kernel`.
- The AICPU KFC Server blocks waiting for AICore Client messages. `Mc2CcKernelLaunch` and the AICore
  kernel must use two different streams, and the AICore stream must be synchronized before the AICPU
  stream.
- For reduce-type communication, set the reduce type on the host through `Mc2SetCcReduceType`, keeping
  it consistent with the `op` passed to `Hccl::ReduceScatter` on the AICore side.
- The AICore side must call `Finalize`. The AICPU KFC Server exits only after receiving this message.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `MC2_MAX_RANK_SIZE`.
