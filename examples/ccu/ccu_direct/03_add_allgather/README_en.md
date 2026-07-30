# CCU Direct Add + AllGather Sample

## Overview

This sample demonstrates how to run Add computation with an AICore vector kernel first, and then use the computed
result as the input of AllGather communication by directly launching the CCU kernel with `<<<>>>`.

The sample creates one rank for each available NPU device. Each rank first runs AICore Add on its local input, and
then synchronizes the computed result to all ranks through CCU AllGather.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
03_add_allgather
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, AICore Add kernel, and CCU AllGather kernel implementation
```

## Sample Description

### Functionality

Each rank input contains `sendCount` FP32 elements. The sample first writes the local Add result to `computeBuf`
through an AICore vector kernel, and then uses `computeBuf` as the CCU AllGather input so that `recvBuf` on every
rank contains all rank computation results.

```text
sendBuf -> AICore Add -> computeBuf -> CCU AllGather -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AllGather in an HCCL communication domain |
| Launch mode | Direct AICore kernel launch with `<<<>>>` and direct CCU kernel launch with `<<<>>>` |
| Data type | FP32 |
| Input size per rank | 256 FP32 elements |
| Add output size | 256 FP32 elements |
| AllGather output size | `rankSize * 256` FP32 elements |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device and initialize the input buffer.
3. Acquire CCU channels, a CCU instance, CCU variables, and CCU events from the HCCL communication domain.
4. Directly launch the AICore vector kernel through `vector_add<<<1, nullptr, streamAiv>>>` to generate the local
   computation result.
5. Obtain the `computeBuf` memory token through `HcommCcuGetMemToken` and prepare CCU task arguments.
6. Directly launch the CCU kernel through `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>`.
7. Synchronize the AIV and CCU streams, copy `recvBuf` back to the host, and print it.
8. Destroy the HCCL communication domain, streams, and device memory.

## Build and Run

Perform the following steps in the sample root directory. This sample supports NPU run mode only.

- Set Environment Variables

  Configure CANN environment variables according to your installation and enable CCU scheduling mode.

  ```bash
  source ${install_path}/cann/set_env.sh
  export HCCL_OP_EXPANSION_MODE=CCU_SCHED
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
  rankId: 0, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
  rankId: 1, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
  ```

  The sample succeeds when `recvBuf` on every rank contains the AICore Add result from all ranks.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE`.
