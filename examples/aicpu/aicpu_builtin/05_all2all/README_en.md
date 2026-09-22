# AICPU MC2 AlltoAll Sample

## Overview

This sample demonstrates how to prepare AICPU communication resources and start an AICPU KFC Server
through the MC2 builtin APIs, and then submit an AlltoAll Client communication request from an AICore
kernel through the HCCL high-level API.

The sample creates one rank for each available NPU device. Each rank sends `dataCount` FP32 elements
to every target rank in the communication domain. On the host, `Mc2AcquireCcResCtx` obtains the
communication resource context and `Mc2CcKernelLaunch` starts the AICPU KFC Server on a dedicated
stream. On the AICore, the same `ccResCtx` is passed to `Hccl::InitV2`, and `Hccl::AlltoAll` completes
the equal-length data exchange.

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
└── main.asc          // Host flow, AICPU resource setup, and AICore communication Client implementation
```

## Sample Description

### Functionality

The send buffer of each rank is divided into `rankSize` blocks by target rank, each containing
`dataCount` FP32 elements. The block destined for rank j is filled with `device * 1000 + j`. The host
configures the `AICPU_TS` communication engine and the FP32 source/destination data types, and obtains
the communication resource context. The AICore `all_to_all_kernel` then calls `Hccl::AlltoAll` to send
block j to rank j and receive the blocks sent by all ranks, concatenated into `recvBuf` in source rank
id order.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server on aicpuStream)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AlltoAll (submit AICPU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | AICPU AlltoAll in an HCCL communication domain |
| Communication engine | `AICPU_TS` (`OpExecuteConfig` value 2) |
| AICPU Server launch | Host-side `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AlltoAll<true>` |
| AICore kernel | `all_to_all_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Data volume per rank pair | 256 FP32 elements |
| Total send/receive size per rank | `rankSize * 256` FP32 elements |
| Supported ranks | No more than `MC2_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICPU and AICore streams, and allocate the send and
   receive buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `AICPU_TS` communication engine
   and the FP32 source and destination data types.
4. Call `Mc2AcquireCcResCtx` to obtain the communication resource context `ccResCtx` and its size from
   the HCCL communication domain.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` to
   start the KFC Server on the AICPU stream. The server blocks waiting for AICore Client messages, so it
   must not share the stream with the AICore kernel.
6. Launch the AICore kernel through `all_to_all_kernel<<<1, nullptr, streamAiv>>>`. Inside the kernel,
   `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on the host.
7. The AICore side calls `Hccl::AlltoAll<true>` to submit the communication Client request, calls
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

  In a two-device run, the block that rank `d` sends to rank `j` is filled with `d * 1000 + j`, and
  block `i` received by rank `d` should be `i * 1000 + d`. The output is similar to:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input blocks: [0 x 256] [1 x 256]
  rankId: 1, input blocks: [1000 x 256] [1001 x 256]
  rankId: 0, recv blocks: [0 x 256] [1000 x 256]
  rankId: 1, recv blocks: [1 x 256] [1001 x 256]
  rankId: 0, validation: PASS
  rankId: 1, validation: PASS
  AlltoAll sample pass
  ```

  The sample succeeds when every rank completes AlltoAll and passes validation.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support
  compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `all_to_all_kernel`.
- The AICPU KFC Server blocks waiting for AICore Client messages. `Mc2CcKernelLaunch` and the AICore
  kernel must use two different streams, and the AICore stream must be synchronized before the AICPU
  stream.
- The AICore side must call `Finalize`. The AICPU KFC Server exits only after receiving this message.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `MC2_MAX_RANK_SIZE`.
