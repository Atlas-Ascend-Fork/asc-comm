# AICore Samples

English | [简体中文](./README.md)

This directory provides usage samples for AICore-side communication APIs, grouped into Hcomm point-to-point communication, Ain one-sided communication, and Jetty communication. The `utils` directory contains host-side helper code shared by the samples and is not a standalone sample.

## Sample List

| Directory | Description |
| --- | --- |
| [ain](./ain/README_en.md) | Ain one-sided communication samples. Demonstrates `Put`/`Get` one-sided operations and collective synchronization over symmetric windows. |
| [hcomm](./hcomm/README_en.md) | Hcomm point-to-point communication samples. Covers ordinary interfaces, batch interfaces, multi-SGE scattered read/write, and functional and performance tests over the AIV direct-driven URMA, SIMT URMA, and RoCE paths. |
| [jetty](./jetty/README_en.md) | Jetty communication samples. Demonstrates AIV Kernel direct-drive and SIMT Kernel flows that build and submit WQEs directly to the Jetty SQ. |

## Usage Notes

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.

See the README in each sample directory for build commands, expected results, and notes.
