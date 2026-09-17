# AICPU MC2 AllGatherReduceScatter Sample

## Overview

This sample demonstrates how to prepare AICPU communication resources and start AICPU KFC Servers
through the MC2 builtin APIs, and then submit AllGather and ReduceScatter Client communication
requests one after another from AICore kernels through the HCCL high-level API, forming a two-stage
composite communication pipeline.

The sample creates one rank for each available NPU device. Each rank provides one FP32 input segment.
AllGather and ReduceScatter are two independent kernels: one resource context (`ccResCtx`) serves a
single communication command, so the two stages hold separate contexts and each starts its own AICPU
KFC Server.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
07_allgather_reducescatter
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, AICPU resource setup, and AICore two-stage communication Client
```

## Sample Description

### Functionality

Each rank input contains `SEND_COUNT` FP32 elements. Stage 1 (AllGather) gathers the inputs of all
ranks into `gatherBuf` (`SEND_COUNT * rankSize` elements) in rank id order. Stage 2 (ReduceScatter)
reduces the `gatherBuf` of all ranks element-wise and scatters the result by rank id, so each rank
receives its own block of `SEND_COUNT` elements. The AllGather output size exactly matches the
ReduceScatter input size, so `gatherBuf` is directly used as the stage 2 input without padding or
splitting.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc* (HCCL_CMD_ALLGATHER)
  -> Mc2AcquireCcResCtx -> agCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server #1 on aicpuStream)
  -> Mc2GetCcArgs/Mc2SetCc* (HCCL_CMD_REDUCE_SCATTER)
  -> Mc2AcquireCcResCtx -> rsCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server #2 on aicpuStream)

AICore:
agCtx -> Hccl::InitV2 -> Hccl::AllGather -> gatherBuf
rsCtx -> Hccl::InitV2 -> Hccl::ReduceScatter -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | Two-stage AICPU AllGather + ReduceScatter in an HCCL communication domain |
| Communication engine | `AICPU_TS` (`OpExecuteConfig` value 2) |
| Reduce type | SUM (stage 2, `Mc2SetCcReduceType` with `HCCL_REDUCE_SUM`) |
| AICPU Server launch | One `Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` per stage |
| Communication requests | AICore-side `Hccl::AllGather<true>` then `Hccl::ReduceScatter<true>` |
| AICore kernels | `all_gather_stage_kernel` and `reduce_scatter_stage_kernel`, both `<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Input size per rank | 256 FP32 elements |
| AllGather output / ReduceScatter input size | `rankSize * 256` FP32 elements |
| Final output size per rank | 256 FP32 elements |
| Supported ranks | No more than `MC2_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICPU and AICore streams, and allocate the input,
   gather, and output buffers.
3. Stage 1: create an MC2 argument object through `Mc2GetCcArgs`, set the `AICPU_TS` communication
   engine and the FP32 source/destination data types, call `Mc2AcquireCcResCtx` with
   `HCCL_CMD_ALLGATHER` to obtain `agCtx`, release the argument object, start the first KFC Server
   through `Mc2CcKernelLaunch`, and launch `all_gather_stage_kernel`.
4. Synchronize the AICore stream first and then the AICPU stream to wait for stage 1 to complete and
   the first server to exit.
5. Stage 2: create a new MC2 argument object with the SUM reduce type added, call
   `Mc2AcquireCcResCtx` with `HCCL_CMD_REDUCE_SCATTER` to obtain `rsCtx`, start the second KFC
   Server, and launch `reduce_scatter_stage_kernel` with the stage 1 `gatherBuf` as input.
6. Synchronize the AICore stream and then the AICPU stream again to wait for stage 2 to complete.
7. Copy `gatherBuf` and `recvBuf` back to the host, and print and validate the results stage by stage.
8. Destroy the HCCL communication domain, both streams, and device memory. Both `ccResCtx` objects
   are owned by the communication domain and do not need to be freed separately.

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

  In a two-device run, the input elements of rank `d` are initialized to `d + 1`. The stage 1
  AllGather output is `[1 x 256, 2 x 256]`. In stage 2, the ReduceScatter input of every rank is the
  identical AllGather output, so after the sum, rank `d` receives `2 * (d + 1)`. The output is
  similar to:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, allgather output: [ 1 1 1 ... 2 2 2 ... ]
  rankId: 0, reducescatter output: [ 2 2 2 ... ]
  rankId: 0, allgather stage validation: PASS
  rankId: 0, expected: 2, reducescatter stage validation: PASS
  rankId: 1, allgather stage validation: PASS
  rankId: 1, expected: 4, reducescatter stage validation: PASS
  AllGatherReduceScatter sample pass
  ```

  The sample succeeds when both stages pass validation on every rank.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support
  compilation only.
- AllGather and ReduceScatter cannot share one `ccResCtx`. Each stage must call
  `Mc2AcquireCcResCtx` with its own communication command and start its own AICPU KFC Server.
- The AICore kernels of the two stages execute in order on the same AICore stream, and the stream
  order guarantees the data dependency of `gatherBuf`. For each stage, synchronize the AICore stream
  before the AICPU stream.
- The AICPU KFC Server blocks waiting for AICore Client messages. `Mc2CcKernelLaunch` and the AICore
  kernels must use two different streams.
- The AICore side must call `Finalize` in each stage. The corresponding AICPU KFC Server exits only
  after receiving this message.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `MC2_MAX_RANK_SIZE`.
