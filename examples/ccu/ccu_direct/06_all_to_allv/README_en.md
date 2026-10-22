# CCU AllToAllV Direct Run Sample

## Overview

This sample implements AllToAllV with an HCCL communication domain, CCU full-mesh channels, and `ccu::write`.
Each rank specifies variable-length data blocks per peer through `sendCounts/sendDispls` and `recvCounts/recvDispls`.

The CCU kernel publishes the output address with the receive offset already applied to each peer; remote data is sent with `ccu::write`, and local data with a single `ccu::local_copy`.

This sample is currently a minimal direct-launch prototype: dynamic `sendBytes/sendOffsets/recvOffsets` are carried by the struct referenced by `ccu_kernel_arg`, and the kernel initializes them with `ccu::variable = uint64_t`. Whether this is equivalent to `ccu::load_arg` must be confirmed by the actual run result of this sample.

The sample supports FP32 only, non-in-place operation, at most 16 ranks, and a per-peer data block of at most 256 MiB.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
06_all_to_allv/
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

The test builds unequal-length data with `count(src, dst) = src + dst + 1` and prints `PASS` or `FAIL` on each rank after checking the output.

## Expected Result

When the sample succeeds, every rank prints `PASS`. If any rank prints `FAIL`, the result does not match the expectation.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and applies only to Ascend 950PR&950DT products.
