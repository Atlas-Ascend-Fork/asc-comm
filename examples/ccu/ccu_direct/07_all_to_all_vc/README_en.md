# CCU Direct AllToAllVC Sample

## Overview

This sample shows how to implement count-matrix-based AllToAllVC with the direct-launch `<<<>>>` approach, based on an HCCL communication domain and CCU dataplane APIs.

The amount of data that each rank sends to its peers is specified by a count matrix, and the received segments are concatenated in source-rank order to form the output. Element `(src, dst)` of the count matrix indicates the number of elements that rank `src` sends to rank `dst`.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
07_all_to_all_vc
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
└── main.asc          // Host flow and CCU AllToAllVC kernel implementation
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU AllToAllVC in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| Data type | FP32 |
| Count matrix | `rankSize * rankSize`, with `count(src, dst) = src + dst + 1` |
| Per-peer data block | No more than 256 MiB |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and obtain the number of NPUs in the current environment.
2. Create one rank for each device, compute `sendCounts/sendDispls` from the row of the local rank in the count matrix and `recvCounts/recvDispls` from the column of the local rank, and initialize the input elements in segments by destination peer.
3. A single-rank scenario degrades to a local copy; in a multi-rank scenario, acquire CCU channel and CCU instance resources based on the HCCL communication domain.
4. Assemble the struct pointed to by `ccu_kernel_arg` to carry dynamic arguments such as `sendBytes/sendOffsets/recvOffsets`.
5. Directly launch the CCU kernel through `CcuAllToAllVCMesh1DKernel<<<schd, instanceHandle, stream>>>`.
6. Inside the kernel, first publish the output address, with the receive offset already applied, to each peer through `ccu::write_variable_with_notify`, and wait for all addresses to be ready through `ccu::notify_wait`; then complete the local data block with `ccu::local_copy`, write the remote data blocks with `ccu::write`, and wait for all transfers to complete.
7. Synchronize the stream, copy the result back to the host, check it block by block by source rank, and print `PASS` or `FAIL`.
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

  In a two-NPU run with `count(src, dst) = src + dst + 1`, successful execution prints information similar to the following:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, output: [ 0 1000 1001 ]
  rankId: 0 PASS
  rankId: 1, output: [ 100 101 1100 1101 1102 ]
  rankId: 1 PASS
  ```

  Every rank prints `PASS`, indicating that the sample ran successfully. If any rank prints `FAIL`, the result does not match the expectation.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
