# CCU Samples

English | [简体中文](./README.md)

This directory provides CCU communication and dataplane samples, covering builtin integration and direct integration.

## Sample List

| Directory | Description |
| --- | --- |
| [ccu_builtin](./ccu_builtin/README_en.md) | MC2 builtin integration samples. The host acquires CCU resources and starts the CCU Server, while the AICore kernel submits communication tasks through high-level HCCL APIs. |
| [ccu_direct](./ccu_direct/README_en.md) | Direct integration samples. These samples start CCU kernels with `<<<>>>` and cover collective communication, basic data movement, and communication-computation fusion. |

## Runtime Constraints

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Direct samples require `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before execution. Follow each sample README.

See the README in each sample directory for build commands, expected results, and notes.
