# CCU Builtin Samples

English | [简体中文](./README.md)

This directory provides CCU samples based on MC2 builtin interfaces. The host prechecks and acquires the CCU resource context and starts the CCU Server, while the AICore kernel submits communication client tasks through high-level HCCL APIs.

## Sample List

| Sample | Description |
| --- | --- |
| [01_allgather](./01_allgather/README_en.md) | Demonstrates a CCU AllGather communication task. |
| [02_allgather_add](./02_allgather_add/README_en.md) | Combines AllGather communication with an AICore Add computation. |
| [03_allreduce](./03_allreduce/README_en.md) | Demonstrates a CCU AllReduce SUM communication task. |
| [04_reducescatter](./04_reducescatter/README_en.md) | Demonstrates a CCU ReduceScatter communication task. |
| [05_all2all](./05_all2all/README_en.md) | Demonstrates equal-length AllToAll data exchange. |
| [06_all2allv](./06_all2allv/README_en.md) | Demonstrates variable-length AllToAllV data exchange. |

## Usage Notes

- The samples support Ascend 950PR&950DT products with CANN 9.2.0 or later.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Each sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE`.

See the README in each sample directory for build commands, expected results, and notes.
