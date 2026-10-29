# CCU Direct Add + AllGather Sample

## Overview

This sample shows how to first perform Add computation with an AICore vector kernel, and then execute AllGather communication by directly launching the CCU kernel with `<<<>>>`, using the computation result as the input.

Each rank first adds the scalar 1.0 to its local input element by element; the results of all ranks are then concatenated in rank order, and each rank obtains a complete copy of the data.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
12_add_allgather
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
└── main.asc          // Host flow, AICore Add kernel, and CCU AllGather kernel implementation
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU AllGather in an HCCL communication domain |
| Invocation | Direct AICore kernel launch with `<<<>>>` and direct CCU kernel launch with `<<<>>>` |
| Data type | FP32 |
| Per-rank input size | 256 FP32 elements |
| Add output size | 256 FP32 elements |
| AllGather output size | `rankSize * 256` FP32 elements |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and obtain the number of NPUs in the current environment.
2. Create one rank for each device and initialize the input buffer.
3. Acquire CCU channel, CCU instance, CCU variable, and CCU event resources based on the HCCL communication domain.
4. Directly launch the AICore vector kernel through `vector_add<<<1, nullptr, streamAiv>>>` to generate the local computation result.
5. Obtain the `computeBuf` memory token through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
6. Directly launch the CCU kernel through `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>` to complete AllGather.
7. Synchronize the AIV and CCU streams, copy `recvBuf` back to the host, and print it.
8. Destroy the HCCL communication domain, streams, and device memory.

## Build and Run

Perform the following steps in the sample root directory to build and run the sample. This sample supports only the NPU run mode.

- Configure environment variables

  Configure the environment variables according to how the CANN development kit package is installed, and enable CCU scheduling mode.

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
  exec build/ccu_add_allgather
  ```

- Build options

  | Option | Possible values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode. This sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to the Ascend 950PR&950DT series products |

- Expected result

  In a two-NPU run, the input elements of rank `d` are initialized to `d + 1`. Successful execution prints information similar to the following:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
  rankId: 1, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
  ```

  The `recvBuf` of each rank contains the AICore Add results of all ranks, indicating that the sample ran successfully.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
- This sample uses all NPU devices visible in the environment, and the number of devices must not exceed `CCU_MAX_RANK_SIZE`.
