# CCU Data Transfer Sample

## Overview

This sample demonstrates three categories of basic data-movement APIs on the A5/V1 CCU dataplane: local copy, remote write, and remote reduce-write. The sample uses a fixed 2 ranks with 4 FP32 input elements per rank, and a compile-time parameter selects the teaching scenario.
The kernel is launched with the `<<<>>>` direct-launch syntax instead of kernel registration and `asccomm_ccu_kernel_launch`.

- Scenario 1 calls the three overloads of `ccu::local_copy`: `local_addr` to `local_addr`, `local_addr` to `ccu_buffer`, and `ccu_buffer` to `local_addr`. Direct copy is used for data movement within the local rank; a `ccu_buffer` can serve as scratch storage, which is useful for first writing data into the CCU buffer and then writing it out from the buffer.
- Scenario 2 calls the two overloads of `ccu::write`: `local_addr` to a remote `remote_addr`, and `ccu_buffer` to a remote `remote_addr`. A remote write is initiated by the local rank and writes directly into peer memory.
- Scenario 3 calls `ccu::write_reduce`, which performs an element-wise SUM reduction during the remote write. The difference from `ccu::write` is that the destination data is not overwritten but accumulated element by element with the source data.

All scenarios use events to track transfer completion; ranks exchange output addresses and tokens over channels and synchronize remote writes before the kernel ends.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
10_ccu_transfer/
├── CMakeLists.txt
├── README.md
├── README_en.md
├── run.sh
└── main.asc
```

## Sample Description

The sample acquires CCU resources from an HCCL communication domain and completes communication or data movement by directly launching a CCU kernel with ASC `<<<>>>`.

## Build and Run

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
SCENARIO_NUM=1 bash run.sh
```

Set `SCENARIO_NUM` to `1`, `2`, or `3` to build and run each of the three teaching scenarios. The sample requires at least 2 visible NPUs in the environment.

## Expected Result

The input of rank0 is `[1, 2, 3, 4]`, and the input of rank1 is `[5, 6, 7, 8]`.

| Scenario | API | rank0 output | rank1 output |
| --- | --- | --- | --- |
| 1 | `local_copy` three overloads | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
| 2 | `write` two overloads | `[5, 6, 7, 8]` | `[1, 2, 3, 4]` |
| 3 | `write_reduce` | `[6, 8, 10, 12]` | `[6, 8, 10, 12]` |

Every rank prints `PASS`. If any rank prints `FAIL`, the transfer result does not match the expectation.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and applies only to Ascend 950PR&950DT products.
