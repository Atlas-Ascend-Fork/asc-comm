# CCU Direct AllGather Sample

## Overview

This sample shows how to implement AllGather with HCCL communication-domain resources and CCU dataplane APIs through the direct-launch syntax `<<<>>>`.

The sample creates one communication-domain rank for each visible NPU. Each rank inputs `rankSize` FP32 elements initialized to its rank ID. After AllGather, each rank output buffer contains all rank inputs in rank order.

Runtime dependencies:

- `libhcomm.so`: provides HCCL communication-domain, channel, and CCU instance query APIs.
- `libasccomm_ccu.so`: provides CCU direct-launch, memory-token, and CCU dataplane APIs.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## Directory Structure

```text
01_allgather/
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // Build-and-run script
└── main.asc          // Host flow and CCU AllGather kernel implementation
```

## Sample Description

### Functionality

Each rank inputs `rankSize` FP32 elements:

```text
rank r sendBuf = [r, r, ..., r]
rank p recvBuf = [segment_0 | segment_1 | ... | segment_N-1]
```

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU AllGather in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| Data type | FP32 |
| Per-rank input size | `rankSize` FP32 elements |
| Output size | `rankSize * rankSize` FP32 elements |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device and initialize the input buffer.
3. Acquire CCU channels and a CCU instance from the HCCL communication domain.
4. Obtain the input memory token through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`.
6. Synchronize the stream, copy the result back to the host, and print it.
7. Destroy the HCCL communication domain, stream, and device memory.

## Build and Run

Set the CANN runtime environment and CCU scheduling mode before running:

```bash
source ${install_path}/cann/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

Run from this sample directory:

```bash
bash run.sh
```

Equivalent manual commands are:

```bash
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
cmake --build . -j$(nproc)
cd ..
exec build/ccu_allgather
```

## Expected Result

In a two-NPU run, successful execution prints information similar to:

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 0 0 ]
rankId: 1, input: [ 1 1 ]
rankId: 0, recvBuf: [ 0 0 1 1 ]
rankId: 1, recvBuf: [ 0 0 1 1 ]
```

Each rank's `recvBuf` contains inputs from all ranks, indicating that the sample succeeded.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and applies only to Ascend 950PR&950DT products.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE`.
