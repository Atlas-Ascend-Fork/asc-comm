# CCU Direct AllReduce Sample

## Overview

This sample shows how to implement AllReduce SUM with the direct-launch `<<<>>>` approach, based on an HCCL communication domain and CCU dataplane APIs.

The inputs of all ranks are summed element by element, and each rank obtains the same sum result.

A compile-time parameter `SCENARIO_NUM` selects the teaching scenario:

- Scenario 1 (recommended): the classic AllReduce SUM flow (scatter-reduce with `write_reduce`); the output of every rank is the element-wise sum of all rank inputs.
- Scenario 2 (inferior to scenario 1 in performance and scalability, mainly for API demonstration): an independent "gather-then-reduce" flow — first `read` moves every rank's input into local `ccu_buffer`s, then `local_reduce` (the N-way `ccu_buffer` overload) reduces them locally; the output is identical to scenario 1.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
04_allreduce
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
└── main.asc          // Host flow and CCU AllReduce kernel implementation
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU AllReduce in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| Teaching scenario | Selected by the compile-time parameter `SCENARIO_NUM`, value 1 or 2 |
| Data type | FP32 (`HCCL_DATA_TYPE_FP32`) |
| Reduce operation | SUM (`HCCL_REDUCE_SUM`) |
| Per-rank input size | `rankSize` FP32 elements |
| Output size | `rankSize` FP32 elements, identical across all ranks |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 16 in this sample |

### Implementation Flow

Scenario 1 (classic AllReduce SUM, scatter-reduce with `write_reduce`):

1. Initialize ACL and the HCCL communication domain, and obtain the number of NPUs in the current environment.
2. Create one rank for each device, with input element `i` initialized to `device * rankSize + i`.
3. Acquire CCU channel and CCU instance resources based on the HCCL communication domain.
4. Obtain the input memory token through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuAllReduceMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`.
6. Inside the kernel, first copy the local input to the local output through `ccu::local_copy`; after synchronization, use `ccu::write_reduce` to accumulate the local input into the outputs of the other ranks by SUM, and wait for all remote writes to complete.
7. Synchronize the stream, copy the result back to the host, and print it.
8. Destroy the HCCL communication domain, stream, and device memory.

Scenario 2 (independent "gather-then-reduce" flow):

1. The pre-synchronization exchanges only the local input address and token (no remote writes, no output address exchange).
2. The local input is copied into `bufs[rankId]` with `ccu::local_copy`.
3. For each peer, `ccu::read` moves the peer's input into `bufs[peer rankId]`, gathering every rank's data locally.
4. `ccu::local_reduce` (the N-way `ccu_buffer` overload) reduces `bufs[0..N-1]` into `bufs[0]`.
5. `bufs[0]` is copied back to the local output — the same result as scenario 1 (the element-wise sum of all rank inputs).

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

  Run the following command in the sample directory. Set `SCENARIO_NUM` to `1` or `2` to build and run each of the two teaching scenarios.

  ```bash
  SCENARIO_NUM=1 bash run.sh
  ```

- Build options

  | Option | Possible values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to the Ascend 950PR&950DT series products |
  | `SCENARIO_NUM` | `1`/`2` | Teaching scenario: 1 classic AllReduce SUM, 2 independent gather-then-reduce flow; default is `1` |

- Expected result

  In a two-NPU run, input element `i` of rank `d` is initialized to `d * rankSize + i`. Both scenarios produce identical output:

  Scenario 1 (default):

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 0 1 ]
  rankId: 1, input: [ 2 3 ]
  rankId: 0, output: [ 2 4 ]
  rankId: 1, output: [ 2 4 ]
  ```

  Scenario 2 uses the independent gather-then-reduce flow; the output is identical to scenario 1:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 0 1 ]
  rankId: 1, input: [ 2 3 ]
  rankId: 0, output: [ 2 4 ]
  rankId: 1, output: [ 2 4 ]
  ```

  The output of each rank is the element-wise sum of all rank inputs, indicating that the sample ran successfully.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- The `ccu_buffer` reduce overload used by scenario 2 supports 2 to 8 ways (the rank count equals the device count); scenario 2 is unsupported beyond 8 devices, while scenario 1 is unaffected.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
