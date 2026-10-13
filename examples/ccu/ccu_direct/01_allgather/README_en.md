# CCU Direct AllGather Sample

## Overview

This sample demonstrates how to implement AllGather collective communication over an HCCL communication domain and
CCU data-plane APIs by directly launching the CCU kernel with `<<<>>>`.

The sample creates one rank for each available NPU device. Each rank provides one FP32 input segment. After the
CCU kernel completes AllGather, the output buffer on every rank contains all rank input segments in rank order.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
01_allgather
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow and CCU AllGather kernel implementation
```

## Sample Description

### Functionality

Each rank input contains `sendCount` FP32 elements. After AllGather, `recvBuf` on every rank stores all rank input
segments in rank order:

```text
rank r sendBuf = segment_r
rank p recvBuf = [segment_0 | segment_1 | ... | segment_N-1]
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AllGather in an HCCL communication domain |
| Launch mode | Direct CCU kernel launch with `<<<>>>` |
| Data type | FP32 |
| Input size per rank | 256 FP32 elements |
| Output size | `rankSize * 256` FP32 elements |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device and initialize the input buffer.
3. Acquire CCU channels and CCU instance resources from the HCCL communication domain.
4. Obtain the input memory token through `HcommCcuGetMemToken` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`.
6. Synchronize the stream, copy the result back to the host, and print it.
7. Destroy the HCCL communication domain, streams, and device memory.

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
  rankId: 0, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
  rankId: 1, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
  ```

  The sample succeeds when `recvBuf` on every rank contains all rank input segments.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE`.
