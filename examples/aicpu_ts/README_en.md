# AICPU_TS Communication Samples

English | [简体中文](./README.md)

This directory provides AICPU_TS-side communication samples, covering custom AICPU_TS communication op development.

## Sample List

| Directory | Description |
| --- | --- |
| [aicpu_ts_direct](./aicpu_ts_direct/README_en.md) | Custom AICPU_TS communication op samples. The host acquires resources through HCCL communication APIs, and AICPU kernels implement collectives with the asc-comm AICPU_TS dataplane primitives. |

## Runtime Constraints

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Custom op samples require matching asc-comm/hcomm source environments, disabled AICPU package verification, and whitelist configuration before running.

See the README in each sample directory for build commands, expected results, and notes.
