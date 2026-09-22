# AICPU MC2 AllGatherAdd Sample

## Overview

This sample demonstrates how to prepare AICPU communication resources and start an AICPU KFC Server
through the MC2 builtin APIs, and then perform an Add computation on an AICore kernel before submitting
an AllGather Client communication request through the HCCL high-level API, overlapping computation
with communication.

The sample creates one rank for each available NPU device. Each rank provides two FP32 inputs `x1`
and `x2`. On the host, `Mc2AcquireCcResCtx` obtains the communication resource context and
`Mc2CcKernelLaunch` starts the AICPU KFC Server on a dedicated stream. On the AICore, the same
`ccResCtx` is passed to `Hccl::InitV2`. The kernel first calls `Hccl::AllGather<false>` to prepare
the communication request without triggering it, then computes `tmp = x1 + x2`, and finally calls
`Commit` to trigger the communication, so the server-side task preparation overlaps with the Add
computation.

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
└── main.asc          // Host flow, AICPU resource setup, and AICore fused compute-communication Client
```

## Sample Description

### Functionality

Each rank input contains `sendCount` FP32 elements in two buffers `x1` and `x2`. The AICore kernel
first calls `AllGather<false>` to send the task information to the server without triggering it, then
computes `tmp = x1 + x2` to fill the send buffer, and finally calls `Commit` to start the data
transfer. The `recvBuf` of every rank gathers the `tmp` of all ranks in rank id order.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server on aicpuStream)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AllGather<false> (Prepare only, no execution)
  -> AddCompute (tmp = x1 + x2, overlapped with server-side preparation)
  -> Hccl::Commit (trigger AllGather)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | AICPU AllGather in an HCCL communication domain, fused with computation |
| Communication engine | `AICPU_TS` (`OpExecuteConfig` value 2) |
| AICPU Server launch | Host-side `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AllGather<false>` + `Commit` |
| AICore kernel | `all_gather_add_kernel<<<1, nullptr, streamAiv>>>` |
| Computation | `tmp = x1 + x2`, tiled with a tile length of 256 |
| Data type | FP32 |
| Input size per rank | 256 FP32 elements for each of `x1` and `x2` |
| AllGather output size | `rankSize * 256` FP32 elements |
| Supported ranks | No more than `MC2_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICPU and AICore streams, and allocate the `x1`,
   `x2`, `tmp`, and receive buffers.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `AICPU_TS` communication engine
   and the FP32 source and destination data types.
4. Call `Mc2AcquireCcResCtx` to obtain the communication resource context `ccResCtx` and its size from
   the HCCL communication domain.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` to
   start the KFC Server on the AICPU stream.
6. Launch the AICore kernel through `all_gather_add_kernel<<<1, nullptr, streamAiv>>>`. Inside the
   kernel, `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on
   the host.
7. The AICore side calls `Hccl::AllGather<false>` to prepare the communication request only, runs
   `AddCompute` to compute `tmp = x1 + x2`, calls `Commit` to notify the server to execute, calls
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

  In a two-device run, the `x1` and `x2` input elements of rank `d` are initialized to `d + 1`, so
  `tmp = x1 + x2` is `2 * (d + 1)`. The output is similar to:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input x1/x2: [ 1 1 1 ... ]
  rankId: 1, input x1/x2: [ 2 2 2 ... ]
  rankId: 0, recvBuf: [ 2 2 2 ... 4 4 4 ... ]
  rankId: 1, recvBuf: [ 2 2 2 ... 4 4 4 ... ]
  rankId: 0, validation: PASS
  rankId: 1, validation: PASS
  AllGatherAdd sample pass
  ```

  The sample succeeds when every rank completes the Add computation and AllGather and passes validation.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support
  compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `all_gather_add_kernel`.
- The AICPU KFC Server blocks waiting for AICore Client messages. `Mc2CcKernelLaunch` and the AICore
  kernel must use two different streams, and the AICore stream must be synchronized before the AICPU
  stream.
- `AllGather<false>` only prepares the request. `Commit` must be called after the send buffer `tmp` is
  fully computed, otherwise the server may transfer data that is not ready.
- The AICore side must call `Finalize`. The AICPU KFC Server exits only after receiving this message.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `MC2_MAX_RANK_SIZE`.
