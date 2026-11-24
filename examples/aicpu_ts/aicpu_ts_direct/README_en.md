# AICPU Custom Communication Op Samples

English | [简体中文](./README.md)

This directory provides custom AICPU_TS collective communication op samples. The host acquires and reuses communication resources through HCCL communication APIs, and AICPU kernels perform collectives with asc-comm POD dataplane primitives.

## Sample List

| Directory | Description |
| --- | --- |
| [allgather](./allgather/README_en.md) | Custom AllGather op that gathers every rank input into each rank output in rank order. |
| [allreduce](./allreduce/README_en.md) | Custom AllReduce op that performs a SUM reduction and outputs the full result. |
| [alltoall](./alltoall/README_en.md) | Custom AlltoAll op that exchanges fixed-size blocks between ranks. |
| [alltoallv](./alltoallv/README_en.md) | Custom AlltoAllV op that exchanges variable-length blocks between ranks. |
| [reduce_scatter](./reduce_scatter/README_en.md) | Custom ReduceScatter op that performs a SUM reduction and scatters the result by rank. |

## Runtime Constraints

- The samples support Ascend 950PR&950DT products in a single-host, N-device topology (N >= 2).
- They use the control-plane-parsed POD entity architecture and require matching asc-comm and hcomm source environments. Incompatible CANN dataplane headers are rejected at CMake configuration time.
- Set `ASC_COMM_SOURCE_DIR`, `HCOMM_SOURCE_DIR`, `ASCCOMM_CPU_LIBRARY`, `ASCCOMM_DEVICE_LIBRARY`, and `HCOMM_DEVICE_LIBRARY` before building to explicitly provide the source trees and host/device libraries.
- AICPU_TS custom op packages can run only after driver signature verification is disabled and the package is added to `ascend_package_load.ini`. See each op README for details.
- If the hccl package is not installed, `${ASCEND_HOME_PATH}/share/info/hccl/version.info` may not exist. hcomm reads this file while initializing a communication domain, and `HcclCommInitRootInfo` fails if it is missing. Install hccl, or create a placeholder file containing `Version=9.2.0` for this sample.

See the README in each op directory for its layout, build and install commands, and expected output.

## Build Entry

This directory preserves the original custom-op build configuration and `build.sh` entry. To build an op, run the command in its README from this directory, for example:

```bash
bash build.sh --vendor=cust --ops=allgather_aicpu --custom_ops_path=./allgather
```

