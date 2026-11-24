# AI Core Samples

English | [简体中文](./README.md)

This directory provides usage samples for AI Core-side communication APIs, grouped into CommRW point-to-point communication, AIN one-sided communication, Jetty communication, and AICore-initiated collective communication (cc). The `utils` directory contains host-side helper code shared by the samples and is not a standalone sample.

## Sample List

| Directory | Description |
| --- | --- |
| [ain](./ain/README_en.md) | AIN one-sided communication samples. Demonstrates `put`/`get` one-sided operations and collective synchronization over symmetric windows. |
| [asccomm](./asccomm/README_en.md) | CommRW point-to-point communication samples. Covers ordinary interfaces, batch interfaces, and functional and performance tests over the AIV direct-driven URMA and RoCE protocol. |
| [jetty](./jetty/README_en.md) | Jetty communication samples. Demonstrates AIV Kernel direct-drive and SIMT Kernel flows that build and submit WQEs directly to the Jetty SQ. |
| [cc](./cc/README_en.md) | Collective communication samples initiated by AICore. MC2 builtin integration samples grouped by communication engine (AICPU_TS and CCU), where the AICore kernel submits communication tasks through high-level HCCL APIs. |

## Usage Notes

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.

See the README in each sample directory for build commands, expected results, and notes.
