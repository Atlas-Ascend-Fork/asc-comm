# AICPU MC2 AlltoAllV Sample

## Overview

This sample demonstrates how to prepare AICPU communication resources and start an AICPU KFC Server
through the MC2 builtin APIs, and then submit a variable-length AlltoAllV Client communication request
from an AICore kernel through the HCCL high-level API.

The sample creates one rank for each available NPU device. Unlike equal-length AlltoAll, the data
volume between each rank pair in AlltoAllV is customized by a formula and differs per pair. On the
host, `Mc2AcquireCcResCtx` obtains the communication resource context and `Mc2CcKernelLaunch` starts
the AICPU KFC Server on a dedicated stream. On the AICore, the same `ccResCtx` is passed to
`Hccl::InitV2`, and the kernel derives the per-peer send/recv counts and displacements from the
formula before calling `Hccl::AlltoAllV` to complete the variable-length data exchange.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
06_all2allv
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, AICPU resource setup, and AICore variable-length communication Client
```

## Sample Description

### Functionality

The send/recv data volumes are customized by the following formulas, shared by the device-side kernel
and the host-side golden computation:

```text
send count from rank i to rank j = BASE_COUNT * (1 + ((2 * i + j) % rankSize))
recv count of rank i from rank j = BASE_COUNT * (1 + ((i + 2 * j) % rankSize))
```

where `BASE_COUNT` is 8. Element `idx` of the send buffer of each rank is filled with
`device * 1000 + idx`. The AICore kernel obtains the local rank information through
`GetRankId`/`GetRankDim`, fills the `sendCounts`/`sdispls`/`recvCounts`/`rdispls` arrays per peer,
and then calls `Hccl::AlltoAllV`.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server on aicpuStream)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> GetRankId/GetRankDim -> derive counts and displacements
  -> Hccl::AlltoAllV (submit AICPU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | AICPU AlltoAllV (variable length) in an HCCL communication domain |
| Communication engine | `AICPU_TS` (`OpExecuteConfig` value 2) |
| AICPU Server launch | Host-side `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AlltoAllV<true>` |
| AICore kernel | `all_to_all_v_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Count base | `BASE_COUNT = 8` FP32 elements |
| Send/recv size per rank | Derived by the formula, different per rank |
| Supported ranks | No more than `MC2_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICPU and AICore streams, compute the total send/recv
   element counts of the local rank by the formula, and allocate the send and receive buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `AICPU_TS` communication engine
   and the FP32 source and destination data types.
4. Call `Mc2AcquireCcResCtx` with `HCCL_CMD_ALLTOALLV` to obtain the communication resource context
   `ccResCtx` and its size from the HCCL communication domain.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` to
   start the KFC Server on the AICPU stream.
6. Launch the AICore kernel through `all_to_all_v_kernel<<<1, nullptr, streamAiv>>>`. Inside the
   kernel, `hccl.InitV2(contextGM, nullptr)` is called, then `GetRankId`/`GetRankDim` are used to fill
   the count and displacement arrays by the formula, and `Hccl::AlltoAllV<true>` submits the request.
7. The AICore side calls `Wait` and `SyncAll` to wait for completion, and finally calls `Finalize` to
   notify the server to exit.
8. Synchronize the AICore stream first and then the AICPU stream, copy `recvBuf` back to the host,
   build the golden result with the same formula, and validate and print element by element.
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

  In a two-device run, rank 0 sends `8 * (1 + (0 % 2)) = 8` elements to rank 0 and
  `8 * (1 + (1 % 2)) = 16` elements to rank 1, so its total send count is 24; rank 0 receives 8
  elements from rank 0 and `8 * (1 + (2 % 2)) = 8` elements from rank 1, so its total recv count is
  16. The output is similar to:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, sendCount: 24, recvCount: 16
  rankId: 1, sendCount: 24, recvCount: 32
  rankId: 0, recvBuf head: [ 0 1 2 ... 7 1000 1001 ... ], validation: PASS
  rankId: 1, recvBuf head: [ 8 9 10 ... 23 ], validation: PASS
  AlltoAllV sample pass
  ```

  The sample succeeds when every rank completes AlltoAllV and passes validation. The `recvBuf head`
  line prints only the first 16 received elements, so all 16 elements on rank 1 come from rank 0.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support
  compilation only.
- The device-side kernel and the host-side golden computation must use exactly the same count
  formulas, otherwise validation fails.
- The count arrays are sized by `MC2_MAX_RANK_SIZE`, and the device count must not exceed it.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `all_to_all_v_kernel`.
- The AICPU KFC Server blocks waiting for AICore Client messages. `Mc2CcKernelLaunch` and the AICore
  kernel must use two different streams, and the AICore stream must be synchronized before the AICPU
  stream.
- The AICore side must call `Finalize`. The AICPU KFC Server exits only after receiving this message.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
