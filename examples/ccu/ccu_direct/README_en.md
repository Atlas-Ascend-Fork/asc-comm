# CCU Direct Samples

English | [简体中文](./README.md)

This directory provides CCU samples integrated through direct ASC invocation. The samples start CCU kernels with `<<<>>>` and show how to sequence communication tasks with AICore computation tasks.

## Sample List

| Sample | Description |
| --- | --- |
| [01_allgather](./01_allgather/README_en.md) | Implements AllGather through direct CCU invocation. |
| [02_allgather_add](./02_allgather_add/README_en.md) | Performs CCU AllGather first, then applies an AICore Add computation to the gathered result. |
| [03_add_allgather](./03_add_allgather/README_en.md) | Performs an AICore Add computation first, then synchronizes the result through CCU AllGather. |

## Usage Notes

- The samples support Ascend 950PR/Ascend 950DT with CANN 9.1.0 or later.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the samples.
- Each sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE`.

See the README in each sample directory for build commands, expected results, and notes.
