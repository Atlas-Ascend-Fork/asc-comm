# CCU Direct AllGather Sample

## Overview

This sample shows how to implement AllGather with the direct-launch `<<<>>>` approach, based on an HCCL communication domain and CCU dataplane APIs.

The inputs of all ranks are concatenated in rank order, and each rank obtains a complete copy of the data.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
01_allgather/
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
└── main.asc          // Host flow and CCU AllGather kernel implementation
```

## Sample Description

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

1. Initialize ACL and the HCCL communication domain, and obtain the number of NPUs in the current environment.
2. Create one rank for each device and initialize the input buffer.
3. Acquire CCU channel and CCU instance resources based on the HCCL communication domain.
4. Obtain the input memory token through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`.
6. Synchronize the stream, copy the result back to the host, and print it.
7. Destroy the HCCL communication domain, stream, and device memory.

## Build and Run

Perform the following steps in the sample root directory to build and run the sample. This sample supports only the NPU run mode.

- Configure environment variables

  Configure the development environment according to how the CANN development kit package is installed, and enable CCU scheduling mode.

  ```bash
  source ${install_path}/cann/set_env.sh
  export HCCL_OP_EXPANSION_MODE=CCU_SCHED
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If no installation directory is specified, the package is installed in `/usr/local/Ascend` by default.

- Run the sample

  Run the following command in the sample directory:

  ```bash
  bash run.sh
  ```

  The equivalent manual commands are:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  cmake --build . -j$(nproc)
  cd ..
  exec build/ccu_allgather
  ```

- Build options

  | Option | Possible values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to the Ascend 950PR&950DT series products |

- Expected result

  In a two-NPU run, successful execution prints information similar to the following:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 0 0 ]
  rankId: 1, input: [ 1 1 ]
  rankId: 0, recvBuf: [ 0 0 1 1 ]
  rankId: 1, recvBuf: [ 0 0 1 1 ]
  ```

  The `recvBuf` of each rank contains the inputs of all ranks, indicating that the sample ran successfully.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
- This sample uses all NPU devices visible in the environment, and the number of devices must not exceed `CCU_MAX_RANK_SIZE`.
