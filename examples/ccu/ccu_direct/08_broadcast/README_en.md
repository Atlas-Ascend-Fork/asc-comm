# CCU Broadcast Direct Run Sample

## Overview

This sample implements Broadcast with an HCCL communication domain, CCU full-mesh channels, and `ccu::write`.
All ranks first exchange local buffer addresses and tokens, and then the root rank writes the complete data directly to the other ranks.
The kernel is launched with the `<<<>>>` direct-launch syntax instead of kernel registration and `asccomm_ccu_kernel_launch`.

This implementation targets flow verification and does not use the scatter+allgather optimization for large data in production HCCL.

The sample supports FP32 only, at most 16 ranks, and a single message of at most 256 MiB.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
08_broadcast/
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
bash run.sh
```

The test uses root rank 0 with a message of `rankSize` elements and prints `PASS` or `FAIL` on all ranks.

## Expected Result

When the sample succeeds, every rank prints `PASS`. If any rank prints `FAIL`, the result does not match the expectation.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and applies only to Ascend 950PR&950DT products.
