# Jetty Samples

English | [简体中文](./README.md)

This directory provides Jetty communication samples. Jetty samples submit WQEs directly to the Jetty SQ through the `jetty/asccomm_jetty.h` and `asccomm_work_queue/asccomm_work_queue_simt.h` interfaces, covering both the AIV Kernel direct-drive and SIMT Kernel execution models.

## Sample List

| Sample | Description |
| --- | --- |
| [01_asccomm_work_queue](./01_asccomm_work_queue/README_en.md) | Demonstrates an AIV Kernel submitting WQEs directly to the Jetty SQ through `asccomm_work_queue::write_nbi` and `asccomm_work_queue::write_nbi_value` in a multi-card scenario, with `drain` used for completion and verification. |
| [02_simt_asccomm_work_queue](./02_simt_asccomm_work_queue/README_en.md) | Demonstrates a SIMT Kernel building and submitting Jetty WQEs directly through `asccomm_work_queue::write_nbi` with thread/warp/group cooperation, covering the multi-warp dispatch batching mode and the mixed SIMT/SIMD publication mode, with stress verification and device-side timing support. |

## Usage Notes

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library.

See the README in each sample directory for build commands, expected results, and notes.
