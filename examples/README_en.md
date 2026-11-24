# asc-comm Samples

English | [简体中文](./README.md)

This directory provides usage samples for asc-comm APIs. The README at each directory level introduces the samples at that level.

## Sample List

| Directory | Description |
| --- | --- |
| [aicore](./aicore/README_en.md) | AICore communication samples, including Hcomm point-to-point communication samples (AIV direct-driven URMA, SIMT URMA, and RoCE paths), Ain one-sided communication samples, and Jetty communication samples. |
| [aicpu_ts](./aicpu_ts/README_en.md) | AICPU_TS communication samples, covering AICPU_TS MC2 builtin integration and custom AICPU_TS communication ops. |
| [ccu](./ccu/README_en.md) | CCU communication and dataplane samples, including direct (custom operator) and builtin (MC2) samples, covering AllGather, AllReduce, ReduceScatter, AlltoAll, and other collective communication as well as AllGather+Add communication-computation fusion. |

## Runtime Constraints

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.

See the README in each sample directory for build commands, expected results, and notes.
