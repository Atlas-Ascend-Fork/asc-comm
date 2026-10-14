# CCU AllGather Direct Sample

## Overview

This sample shows how to use the ASC direct-launch syntax `<<<>>>` to implement AllGather with HCCL communication-domain resources and CCU dataplane APIs.

The sample creates one communication-domain rank for each visible NPU. Every rank sends `rankSize` FP32 elements initialized to its rank ID. After the CCU AllGather completes, each rank's receive buffer contains all rank inputs in rank order.

Runtime dependencies:
- `libhcomm.so`: provides HCCL communication-domain, channel and CCU instance query APIs.
- `libasccomm_ccu.so`: provides CCU direct-launch support, memory-token APIs and CCU dataplane programming APIs.

## Directory Structure

```text
01_allgather/
├── CMakeLists.txt
├── README.md
├── README_en.md
├── run.sh
└── main.asc
```

## Build And Run

Set the CANN runtime environment and CCU scheduling mode before building:

```bash
source ${install_path}/cann/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

Run from this directory:

```bash
bash run.sh
```

Equivalent manual commands are:

```bash
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
cmake --build . -j$(nproc)
cd ..
exec build/ccu_allgather
```

Supported products and CANN version: Ascend 950PR/Ascend 950DT, CANN 9.1.0 or later. At least 2 NPUs are required to run the sample. A single-NPU environment can only perform a build check.

## Sample Result

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 0 0 ]
rankId: 1, input: [ 1 1 ]
rankId: 0, recvBuf: [ 0 0 1 1 ]
rankId: 1, recvBuf: [ 0 0 1 1 ]
```

The direct-launch path replaces `asccomm_ccu_kernel_register*` and `asccomm_ccu_kernel_launch` with `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>(...)`.
