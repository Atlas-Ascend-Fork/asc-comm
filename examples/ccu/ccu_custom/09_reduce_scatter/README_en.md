# CCU ReduceScatter Direct Run Sample

## Overview

This sample shows how to implement ReduceScatter SUM with HCCL communication-domain APIs and CCU dataplane APIs.

The input of each rank is split into `rankSize` segments by destination rank. After same-position data of all ranks is summed, rank `r` keeps only segment `r`:

```text
rank r recvBuf = SUM(segment_r of all ranks)
```

The CCU kernel first copies the input segment of the local rank to the local output with `GroupCopy`, and then reduces the other input segments to the corresponding ranks' outputs with `ccu::write_reduce`.


## Directory Structure

```text
09_reduce_scatter/
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

The test uses `recvCount = rankSize`, and input element `i` of rank `d` is initialized to `d * 100 + i`. In a two-card run:

```text
rankId: 0, input: [ 0 1 2 3 ]
rankId: 1, input: [ 100 101 102 103 ]
rankId: 0, output: [ 100 102 ]
rankId: 0 PASS
rankId: 1, output: [ 104 106 ]
rankId: 1 PASS
```
