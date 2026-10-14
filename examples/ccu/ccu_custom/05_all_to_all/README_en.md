# CCU AllToAll Direct Run Sample

## Overview

This sample shows how to implement equal-length AllToAll with HCCL communication-domain APIs and CCU dataplane APIs.

The input of each rank is split into `rankSize` segments by destination rank, and `perCount` elements are sent to each destination rank. The output of a destination rank stores segments in source-rank order:

```text
rank r sendBuf = [to_rank_0 | to_rank_1 | ... | to_rank_N-1]
rank p recvBuf = [from_rank_0 | from_rank_1 | ... | from_rank_N-1]
```

The CCU kernel uses `GroupCopy` for the local segment copy and `ccu::write` to write the other input segments to remote outputs. AllToAll performs no reduction.

Sample constraints:

- `HCCL_DATA_TYPE_FP32` only
- The element count from each source to each destination is specified by `perCount`
- Input and output use separate device buffers; in-place operation is not supported
- Single-host 1D full mesh with at most 16 ranks
- Run with `HCCL_OP_EXPANSION_MODE=CCU_SCHED`

## Directory Structure

```text
05_all_to_all/
├── CMakeLists.txt
├── README.md
├── run.sh
├── main.cc
├── op_host/
│   ├── CMakeLists.txt
│   ├── alg_resource.cc
│   ├── alg_resource.h
│   ├── exec_op.cc
│   └── exec_op.h
└── op_kernel_ccu/
    ├── CMakeLists.txt
    ├── ccu_kernel.cc
    └── ccu_kernel.h
```

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

## Sample Result

The test uses `perCount = rankSize`, and input element `i` of rank `d` is initialized to `d * 100 + i`. In a two-card run:

```text
rankId: 0, input: [ 0 1 2 3 ]
rankId: 1, input: [ 100 101 102 103 ]
rankId: 0, output: [ 0 1 100 101 ]
rankId: 0 PASS
rankId: 1, output: [ 2 3 102 103 ]
rankId: 1 PASS
```
