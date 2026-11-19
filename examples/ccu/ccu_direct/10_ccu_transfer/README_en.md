# CCU Direct Data Transfer Sample

## Overview

This sample demonstrates the basic data-movement, load/store, and control-flow APIs on the CCU dataplane across nine application scenarios. The sample uses a fixed 2 ranks, with 4 FP32 input elements per rank, and selects the sample scenario through the compile-time parameter `SCENARIO_NUM`.

- Scenario 1 calls the three overloads of `ccu::local_copy`: local on-chip memory -> local on-chip memory, local on-chip memory -> local CCU Buffer, and local CCU Buffer -> local on-chip memory. The "via CCU Buffer" leg is completed by a `ccu::call_func` function block: five variables (source/destination addresses and tokens, and the length) are passed in as arguments, while the address, `ccu_buffer`, and event resources are created inside the function block. The block is synthesized on the first call, so its instructions are generated only once.
- Scenario 2 calls the two overloads of `ccu::write`: local on-chip memory -> remote on-chip memory, and local CCU Buffer -> remote on-chip memory.
- Scenario 3 calls `ccu::write_reduce`, which supports local on-chip memory -> remote on-chip memory and performs an element-wise SUM reduction during the remote write.
- Scenario 4 calls `ccu::load`/`ccu::store` and `ccu::address`: `address` is a standalone address resource that supports indirect assignment from a `variable`, immediate assignment, and addition between address/variable operands before being embedded into a `local_addr`; `load`/`store` cover both immediate-address and variable-indirect addressing, in both single-variable and `array<variable>` bulk forms.
- Scenario 5 calls the two overloads of `ccu::read`: remote on-chip memory -> local on-chip memory, and remote on-chip memory -> local CCU Buffer. Note that the parameter order of `read` is "destination first, source last", which is opposite to `ccu::write`; this scenario additionally exchanges input addresses with the peer.
- Scenario 6 calls `ccu::read_reduce`: the local output is first initialized, then read from the peer and reduced in place with SUM, producing the sum of both inputs.
- Scenario 7 verifies the software control-flow macros with a path-bit accumulator: `CCU_DO` + `CCU_WHILE` loops once and sets 0b0001; the standalone `CCU_WHILE` loops once and sets 0b0010; the standalone `CCU_IF` takes the true branch and sets 0b0100; `CCU_IF` + `CCU_ELSE` takes the else branch and sets 0b1000. When all four paths execute as expected, `result == 0b1111` and the final gate lets the staged copy land in the output (output = input); any mismatch corrupts the output so the check fails. It also demonstrates the local_notify overloads of `event_record("tag", mask)`/`event_wait("tag", mask)` (same-die cross-kernel synchronization).
- Scenario 8 calls `ccu::local_reduce` (local on-chip memory -> local on-chip memory in-place reduction): the output is zero-initialized, then reduced twice with SUM and MAX, and the result stays equal to the input.
- Scenario 9 calls the config-based forms of `ccu::loop`/`ccu::loop_group` (iteration parameters fixed at compile time through `ccu_loop_config`/`ccu_loop_group_config`), with a zero-argument lambda body that only performs local copy and wait.

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
├── main.asc          // Host flow
└── ccu_transfer_kernel.h // Kernel-side implementation
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU data transfer in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| application scenario | Selected through the compile-time parameter `SCENARIO_NUM`, with values 1 through 9 |
| Data type | FP32 |
| Per-rank input size | 4 FP32 elements |
| Supported ranks | Fixed at 2 |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and create a fixed 2 ranks.
2. Initialize the input of rank0 to `[1, 2, 3, 4]` and the input of rank1 to `[5, 6, 7, 8]`.
3. Acquire CCU channel and CCU instance resources based on the HCCL communication domain.
4. Obtain the input and output memory tokens through `HcommCcuGetMemToken` and prepare CCU task arguments.
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

  Run the following command in the sample directory.   Set `SCENARIO_NUM` to any value from `1` to `9` to build and run each of the nine application scenarios.

  ```bash
  SCENARIO_NUM=1 bash run.sh
  ```

- Build options

  | Option | Possible values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to the Ascend 950PR&950DT series products |
  | `SCENARIO_NUM` | `1`~`9` | application scenario: 1 local copy, 2 remote write, 3 remote reduce-write, 4 load/store/address, 5 read, 6 read reduce, 7 control-flow macros + local notify, 8 local in-place reduce, 9 config-based hardware loop. The default value is `1` |

- Expected result

  The input of rank0 is `[1, 2, 3, 4]`, and the input of rank1 is `[5, 6, 7, 8]`. The expected outputs of the nine scenarios are as follows:

  | Scenario | API | rank0 output | rank1 output |
  | --- | --- | --- | --- |
  | 1 | Three `local_copy` overloads (staged leg via `call_func`) | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
  | 2 | Two `write` overloads | `[5, 6, 7, 8]` | `[1, 2, 3, 4]` |
  | 3 | `write_reduce` | `[6, 8, 10, 12]` | `[6, 8, 10, 12]` |
  | 4 | `load`/`store` + `address` | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
  | 5 | Two `read` overloads | `[5, 6, 7, 8]` | `[1, 2, 3, 4]` |
  | 6 | `read_reduce` | `[6, 8, 10, 12]` | `[6, 8, 10, 12]` |
  | 7 | Control-flow macros + `local_notify` | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
  | 8 | `local_reduce` (in-place SUM + MAX) | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
  | 9 | Config-based `loop`/`loop_group` | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |

  Every rank prints `PASS`. If any rank prints `FAIL`, the transfer result does not match the expectation.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
