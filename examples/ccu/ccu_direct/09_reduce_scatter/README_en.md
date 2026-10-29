# CCU Direct ReduceScatter Sample

## Overview

This sample shows how to implement ReduceScatter SUM with the direct-launch `<<<>>>` approach, based on an HCCL communication domain and CCU dataplane APIs.

The inputs of all ranks are summed element by element and then split into segments by rank; each rank obtains the sum result of its corresponding segment.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
09_reduce_scatter
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
└── main.asc          // Host flow and CCU ReduceScatter kernel implementation
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU ReduceScatter in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| Data type | FP32 (`HCCL_DATA_TYPE_FP32`) |
| Reduce operation | SUM (`HCCL_REDUCE_SUM`) |
| Per-rank input size | `rankSize * rankSize` FP32 elements |
| Output size | `rankSize` FP32 elements |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and obtain the number of NPUs in the current environment.
2. Create one rank for each device, with input element `i` initialized to `device * 100 + i`.
3. Acquire CCU channel and CCU instance resources based on the HCCL communication domain.
4. Obtain the input memory token through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuReduceScatterMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`.
6. Inside the kernel, first copy the input segment of the local rank to the local output through `GroupCopy`; after synchronization, reduce the other input segments to the outputs of the corresponding ranks by SUM through `ccu::write_reduce`, and wait for all remote writes to complete.
7. Synchronize the stream, copy the result back to the host, check it element by element, and print `PASS` or `FAIL`.
8. Destroy the HCCL communication domain, stream, and device memory.

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

  `run.sh` configures CMake, builds the sample, and runs it.

- Build options

  | Option | Possible values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to the Ascend 950PR&950DT series products |

- Expected result

  In a two-NPU run, input element `i` of rank `d` is initialized to `d * 100 + i`. Successful execution prints information similar to the following:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 0 1 2 3 ]
  rankId: 1, input: [ 100 101 102 103 ]
  rankId: 0, output: [ 100 102 ]
  rankId: 0 PASS
  rankId: 1, output: [ 104 106 ]
  rankId: 1 PASS
  ```

  The output of rank `r` is the element-wise sum of segment `r` of all rank inputs, and each rank prints `PASS`, indicating that the sample ran successfully.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
