# CCU Direct Broadcast Sample

## Overview

This sample shows how to implement Broadcast with the direct-launch `<<<>>>` approach, based on an HCCL communication domain and CCU dataplane APIs.

The root rank broadcasts its complete input to all ranks.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
08_broadcast
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
└── main.asc          // Host flow and CCU Broadcast kernel implementation
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU Broadcast in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| Root rank | 0 |
| Data type | FP32 |
| Per-rank input size | `rankSize` FP32 elements |
| Data volume per transfer | No more than 256 MiB |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and obtain the number of NPUs in the current environment.
2. Create one rank for each device. The input of the root is initialized to the element index `i`, and the input of non-root ranks is initialized to `-1`.
3. Acquire CCU channel and CCU instance resources based on the HCCL communication domain.
4. Obtain the buffer memory token through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuBroadcastMesh1DKernel<<<schd, insHandle, stream>>>`.
6. Inside the kernel, first exchange the buffer addresses and tokens of all ranks through `ccu::write_variable_with_notify` and `ccu::notify_wait`; then the root rank writes the complete data to the other ranks through `ccu::write`, and waits for all remote writes to complete.
7. Synchronize the stream, copy the result back to the host, check that the output equals the input of the root, and print `PASS` or `FAIL`.
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

  In a two-NPU run, the input of root rank 0 is `[0, 1]`. Successful execution prints information similar to the following:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, output: [ 0 1 ]
  rankId: 0 PASS
  rankId: 1, output: [ 0 1 ]
  rankId: 1 PASS
  ```

  Every rank prints `PASS`, indicating that the sample ran successfully. If any rank prints `FAIL`, the result does not match the expectation.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
