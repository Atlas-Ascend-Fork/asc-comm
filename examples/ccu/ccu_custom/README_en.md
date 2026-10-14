# CCU Custom Kernel Samples

English | [简体中文](./README.md)

This directory provides samples where the host directly registers and launches custom CCU kernels. The samples acquire CCU threads, channels, and memory tokens through HCCL communicators, register kernels with `asccomm_ccu_kernel_register*`, and launch them with `asccomm_ccu_kernel_launch`.

## Sample List

| Sample | Description |
| --- | --- |
| [01_allgather](./01_allgather/README_en.md) | Demonstrates a custom CCU AllGather kernel. |
| [02_allgather_mesh1d_mem2mem](./02_allgather_mesh1d_mem2mem/README_en.md) | Demonstrates the Mesh1D Mem2Mem AllGather dataplane. |
| [03_allgather_nhr1d_mem2mem](./03_allgather_nhr1d_mem2mem/README_en.md) | Demonstrates the NHR1D Mem2Mem AllGather dataplane. |
| [04_allreduce](./04_allreduce/README_en.md) | Demonstrates a custom AllReduce SUM kernel. |
| [05_all_to_all](./05_all_to_all/README_en.md) | Demonstrates equal-length AllToAll data exchange. |
| [06_all_to_allv](./06_all_to_allv/README_en.md) | Demonstrates variable-length AllToAllV data exchange. |
| [07_all_to_all_vc](./07_all_to_all_vc/README_en.md) | Demonstrates AllToAllVC with a shared count matrix. |
| [08_broadcast](./08_broadcast/README_en.md) | Demonstrates broadcasting data from the root rank to other ranks. |
| [09_reduce_scatter](./09_reduce_scatter/README_en.md) | Demonstrates ReduceScatter SUM. |
| [10_ccu_transfer](./10_ccu_transfer/README_en.md) | Demonstrates local copy, remote write, and remote reduce-write data movement APIs. |

## Usage Notes

- The samples support Ascend 950PR/Ascend 950DT. See each sample README for the exact CANN version and scale constraints.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running most samples. Follow each sample README.

See the README in each sample directory for build commands, expected results, and notes.
