# CCU Direct Samples

English | [简体中文](./README.md)

This directory provides direct-launch samples for CCU integration. Samples start CCU kernels with `<<<>>>` and cover basic collective communication, variable-length communication, basic data movement APIs, and ways to combine communication tasks with AICore computation tasks.

## Sample List

| Sample | Description |
| --- | --- |
| [01_allgather](./01_allgather/README_en.md) | Demonstrates AllGather implemented through CCU direct launch. |
| [02_allgather_mesh1d_mem2mem](./02_allgather_mesh1d_mem2mem/README_en.md) | Demonstrates the Mesh1D Mem2Mem AllGather dataplane. |
| [03_allgather_nhr1d_mem2mem](./03_allgather_nhr1d_mem2mem/README_en.md) | Demonstrates the NHR1D Mem2Mem AllGather dataplane. |
| [04_allreduce](./04_allreduce/README_en.md) | Demonstrates AllReduce SUM. |
| [05_all_to_all](./05_all_to_all/README_en.md) | Demonstrates equal-length AllToAll. |
| [06_all_to_allv](./06_all_to_allv/README_en.md) | Demonstrates variable-length AllToAllV. |
| [07_all_to_all_vc](./07_all_to_all_vc/README_en.md) | Demonstrates AllToAllVC based on a count matrix. |
| [08_broadcast](./08_broadcast/README_en.md) | Demonstrates Broadcast. |
| [09_reduce_scatter](./09_reduce_scatter/README_en.md) | Demonstrates ReduceScatter SUM. |
| [10_ccu_transfer](./10_ccu_transfer/README_en.md) | Demonstrates local copy, remote write, and remote reduce-write. |
| [11_allgather_add](./11_allgather_add/README_en.md) | Performs CCU AllGather first, and then AICore Add computation on the gathered results. |
| [12_add_allgather](./12_add_allgather/README_en.md) | Performs AICore Add computation first, and then synchronizes the computation results through CCU AllGather. |

## Usage Notes

- The samples support the Ascend 950PR&950DT series products and require CANN 9.1.0 or later.
- At least two NPUs are required to run the samples. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before execution.
- Samples use all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE`.

See the README in each sample directory for build commands, execution results, and notes.
