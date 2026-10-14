# CCU AllReduce Direct Run Sample

## Overview

This sample shows how to implement a custom AllReduce SUM operation with HCCL communication-domain APIs and CCU dataplane APIs.

Sample constraints:

- The sample uses `HCCL_DATA_TYPE_FP32` only
- The sample uses `HCCL_REDUCE_SUM` only
- The sample uses separate `sendBuf` and `recvBuf`
- Single-host multi-card mesh communication; every rank ends up with the same element-wise sum
- Run with `HCCL_OP_EXPANSION_MODE=CCU_SCHED`

Algorithm flow:

1. Each rank obtains a CCU token in the same way as the allgather sample.
2. Each rank copies its local input to its local output as its initial contribution to the result.
3. All ranks synchronize to ensure every output has been initialized.
4. Each rank uses `ccu::write_reduce(..., HCCL_REDUCE_SUM, ...)` to accumulate its local input into the outputs of the other ranks.
5. After all ranks finish synchronizing, the output of every rank is the element-wise sum of all rank inputs.

## Directory Structure

```text
04_allreduce/
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

`run.sh` configures CMake, builds, and runs:

```bash
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DASCEND_HOME_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"
exec "${BUILD_DIR}/ccu_allreduce"
```

## Sample Result

In the test program, the input of every rank is initialized to `[0, 1, ..., devCount-1]`. Therefore, in an 8-card run, the output of every rank after AllReduce SUM is:

```text
Found 8 NPU device(s) available
rankId: 0, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 1, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 2, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 3, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 4, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 5, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 6, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 7, output: [ 0 8 16 24 32 40 48 56 ]
```
