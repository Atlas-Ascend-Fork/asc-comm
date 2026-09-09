# CCU AllGather Direct Run Sample

## Overview

This sample shows how to implement an AllGather operation with HCCL communication-domain APIs and CCU dataplane APIs.

Runtime dependencies:
- `libhcomm.so`: provides HCCL communication-domain, CCU control-plane, thread and channel resource APIs.
- `libasccomm_ccu_dataplane.so`: provides `asccomm_ccu_kernel_register*`, `asccomm_ccu_kernel_launch`, `asccomm_ccu_get_mem_token` and CCU dataplane programming APIs.

The sample keeps the original AllGather kernel-side `GroupCopy` implementation for the local copy path.

## Directory Structure

```text
ccu_allgather/
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

## Build And Run

Set the CANN runtime environment and CCU scheduling mode before running:

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

Then run the sample from this directory:

```bash
bash run.sh
```

`run.sh` removes the old `build` directory, configures CMake, builds the executable and runs it:

```bash
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DASCEND_HOME_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"
exec "${BUILD_DIR}/ccu_allgather"
```

## Result Example

Each rank sends `rankSize` FP32 elements. The input of each rank is initialized to that rank ID.

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 0 0 ]
rankId: 1, input: [ 1 1 ]
rankId: 0, output: [ 0 0 1 1 ]
rankId: 1, output: [ 0 0 1 1 ]
```
