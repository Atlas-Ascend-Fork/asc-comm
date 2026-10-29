# CCU Direct Data Transfer Sample

## Overview

This sample demonstrates three categories of basic data-movement APIs on the CCU dataplane: local copy, remote write, and remote reduce-write. The sample uses a fixed 2 ranks, with 4 FP32 input elements per rank, and selects the sample scenario through the compile-time parameter `SCENARIO_NUM`.

- Scenario 1 calls the three overloads of `ccu::local_copy`: local on-chip memory -> local on-chip memory, local on-chip memory -> local CCU Buffer, and local CCU Buffer -> local on-chip memory.
- Scenario 2 calls the two overloads of `ccu::write`: local on-chip memory -> remote on-chip memory, and local CCU Buffer -> remote on-chip memory.
- Scenario 3 calls `ccu::write_reduce`, which supports local on-chip memory -> remote on-chip memory and performs an element-wise SUM reduction during the remote write.

All scenarios use events to track transfer completion. Ranks exchange output addresses and tokens through channels and synchronize remote writes before the kernel ends.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
10_ccu_transfer
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
└── main.asc          // Host flow and CCU data transfer kernel implementation
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU data transfer in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| Teaching scenario | Selected through the compile-time parameter `SCENARIO_NUM`, with values 1/2/3 |
| Data type | FP32 |
| Per-rank input size | 4 FP32 elements |
| Supported ranks | Fixed at 2 |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and create a fixed 2 ranks.
2. Initialize the input of rank0 to `[1, 2, 3, 4]` and the input of rank1 to `[5, 6, 7, 8]`.
3. Acquire CCU channel and CCU instance resources based on the HCCL communication domain.
4. Obtain the input and output memory tokens through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuTransferKernel<<<schd, insHandle, stream>>>`.
6. Inside the kernel, first exchange the output addresses and tokens; then execute `ccu::local_copy`, `ccu::write`, or `ccu::write_reduce` according to the scenario compiled from `SCENARIO_NUM`, and use events to track transfer completion and synchronize remote writes.
7. Synchronize the stream, copy the result back to the host, check it against the scenario expectation, and print `PASS` or `FAIL`.
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

  Run the following command in the sample directory. Set `SCENARIO_NUM` to `1`, `2`, or `3` to build and run each of the three teaching scenarios.

  ```bash
  SCENARIO_NUM=1 bash run.sh
  ```

- Build options

  | Option | Possible values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to the Ascend 950PR&950DT series products |
  | `SCENARIO_NUM` | `1`/`2`/`3` | Teaching scenario: 1 for local copy, 2 for remote write, and 3 for remote reduce-write. The default value is `1` |

- Expected result

  The input of rank0 is `[1, 2, 3, 4]`, and the input of rank1 is `[5, 6, 7, 8]`. The expected outputs of the three scenarios are as follows:

  | Scenario | API | rank0 output | rank1 output |
  | --- | --- | --- | --- |
  | 1 | Three `local_copy` overloads | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
  | 2 | Two `write` overloads | `[5, 6, 7, 8]` | `[1, 2, 3, 4]` |
  | 3 | `write_reduce` | `[6, 8, 10, 12]` | `[6, 8, 10, 12]` |

  Every rank prints `PASS`. If any rank prints `FAIL`, the transfer result does not match the expectation.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
