# CCU AllToAllVC Direct Run Sample

## Overview

This sample implements AllToAllVC with an HCCL communication domain, CCU full-mesh channels, and `ccu::write`.
All ranks share one `rankSize * rankSize` count matrix; element `(src, dst)` specifies the number of elements src sends to dst.

The CCU kernel publishes the output address with the receive offset already applied to each peer; remote data is sent with `ccu::write`, and local data with a single `ccu::local_copy`.

Dynamic `sendBytes/sendOffsets/recvOffsets` are carried by the struct referenced by `ccu_kernel_arg`, and the direct `<<<>>>` call passes only the kernel argument. This reuses the `ccu::variable = uint64_t` initialization approach verified in sample 06.

The sample supports FP32 only, non-in-place operation, at most 16 ranks, and a per-peer data block of at most 256 MiB.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
07_all_to_all_vc/
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

The host computes the send layout from its row of the count matrix and the receive layout from its column. The test builds unequal-length data with
`count(src, dst) = src + dst + 1` and prints `PASS` or `FAIL`.

## Expected Result

When the sample succeeds, every rank prints `PASS`. If any rank prints `FAIL`, the result does not match the expectation.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and applies only to Ascend 950PR&950DT products.
