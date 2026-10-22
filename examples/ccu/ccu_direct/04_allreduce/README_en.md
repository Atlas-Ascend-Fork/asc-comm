# CCU AllReduce Direct Run Sample

## Overview

This sample shows how to implement a custom AllReduce SUM operation with HCCL communication-domain APIs and CCU dataplane APIs.
The kernel is launched with the `<<<>>>` direct-launch syntax instead of kernel registration and `asccomm_ccu_kernel_launch`.

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

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
04_allreduce/
├── CMakeLists.txt
├── README.md
├── run.sh
├── README_en.md
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

`run.sh` configures CMake, builds, and runs:

```bash
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build "${BUILD_DIR}" -j"$(nproc)"
exec "${BUILD_DIR}/ccu_allreduce"
```

## Expected Result

In the test program, the input of every rank is initialized to `[0, 1, ..., devCount-1]`. Therefore, in a two-card run, the output of every rank after AllReduce SUM is:

```text
Found 2 NPU device(s) available
rankId: 0, output: [ 0 2 4 6 ]
rankId: 1, output: [ 0 2 4 6 ]
```

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- The sample uses `dav-3510` by default and applies only to Ascend 950PR&950DT products.
