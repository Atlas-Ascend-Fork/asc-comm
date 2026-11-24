# Collective Communication Samples (AICore-Initiated)

English | [简体中文](./README.md)

This directory provides collective communication samples initiated by AICore. The host acquires communication engine resources and starts the corresponding server, while the AICore kernel submits communication client tasks through high-level HCCL APIs. Samples are grouped by communication engine (AICPU_TS and CCU), and currently cover MC2 builtin integration.

## Sample List

| Directory | Description |
| --- | --- |
| [aicpu_ts_builtin](./aicpu_ts_builtin/README_en.md) | AICPU_TS MC2 builtin integration samples, covering AllGather, AllGatherAdd, AllReduce, ReduceScatter, AlltoAll, and AlltoAllV. |
| [ccu_builtin](./ccu_builtin/README_en.md) | CCU MC2 builtin integration samples, covering AllGather, AllGatherAdd, AllReduce, ReduceScatter, AlltoAll, and AlltoAllV. |

## Usage Notes

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- `HCCL_OP_EXPANSION_MODE=CCU_SCHED` must be set explicitly before running the CCU builtin samples. Follow each sample README.

See the README in each sample directory for build commands, expected results, and notes.
