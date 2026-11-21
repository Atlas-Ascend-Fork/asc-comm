# AICPU Samples

English | [简体中文](./README.md)

This directory provides AICPU communication samples, covering MC2 builtin integration and custom AICPU communication ops.

## Sample List

| Directory | Description |
| --- | --- |
| [aicpu_builtin](./aicpu_builtin/01_allgather/README_en.md) | MC2 builtin integration samples. The host prepares AICPU resources and starts the KFC Server, while the AICore kernel submits communication tasks through high-level HCCL APIs. |
| [aicpu_direct](./aicpu_direct/README_en.md) | Custom AICPU communication op samples. The host acquires resources through HCCL communication APIs, and AICPU kernels implement collectives with asc-comm POD dataplane primitives. |

## Runtime Constraints

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments support compilation verification only.
- Custom op samples require matching asc-comm/hcomm source environments, disabled AICPU package verification, and whitelist configuration before running.

See the README in each sample directory for build commands, expected results, and notes.

