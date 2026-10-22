# CCU ReduceScatter Direct Run Sample

## Overview

This sample shows how to implement ReduceScatter SUM with HCCL communication-domain APIs and CCU dataplane APIs.
The kernel is launched with the `<<<>>>` direct-launch syntax instead of kernel registration and `asccomm_ccu_kernel_launch`.

The input of each rank is split into `rankSize` segments by destination rank. After same-position data of all ranks is summed, rank `r` keeps only segment `r`:

```text
rank r recvBuf = SUM(segment_r of all ranks)
```

The CCU kernel first copies the input segment of the local rank to the local output with `GroupCopy`, and then reduces the other input segments to the corresponding ranks' outputs with `ccu::write_reduce`.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
09_reduce_scatter/
├── CMakeLists.txt
├── README.md
├── README_en.md
├── run.sh
└── main.asc
```

## Sample Description

The sample acquires CCU resources from an HCCL communication domain and completes communication or data movement by directly launching a CCU kernel with ASC `<<<>>>`.

## Build and Run

Set the CANN runtime environment and CCU scheduling mode before running:

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

Then run the sample from the sample directory:

```bash
bash run.sh
```

`run.sh` configures CMake, builds, and runs the sample.

## Expected Result

The test uses `recvCount = rankSize`, and input element `i` of rank `d` is initialized to `d * 100 + i`. In a two-card run:

```text
rankId: 0, input: [ 0 1 2 3 ]
rankId: 1, input: [ 100 101 102 103 ]
rankId: 0, output: [ 100 102 ]
rankId: 0 PASS
rankId: 1, output: [ 104 106 ]
rankId: 1 PASS
```

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and applies only to Ascend 950PR&950DT products.
