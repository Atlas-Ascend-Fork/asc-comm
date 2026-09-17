# CCU Samples

English | [简体中文](./README.md)

This directory provides CCU communication and dataplane samples, covering builtin integration, direct ASC integration, and custom kernels.

## Sample List

| Directory | Description |
| --- | --- |
| [ccu_builtin](./ccu_builtin/README_en.md) | MC2 builtin integration samples. The host acquires CCU resources and starts the CCU Server, while the AICore kernel submits communication tasks through high-level HCCL APIs. |
| [ccu_direct](./ccu_direct/README_en.md) | Direct ASC integration samples. These samples start CCU kernels with `<<<>>>` and combine communication with AICore computation. |
| [ccu_custom](./ccu_custom/README_en.md) | Custom CCU kernel samples. The host directly registers and launches kernels to demonstrate collective communication and basic data movement APIs. |

## Runtime Constraints

- The samples support Ascend 950PR/Ascend 950DT. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Direct and custom kernel samples usually require `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before execution. Follow each sample README.

See the README in each sample directory for build commands, expected results, and notes.
