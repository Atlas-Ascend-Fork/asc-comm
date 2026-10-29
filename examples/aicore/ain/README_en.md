# Ain Samples

English | [简体中文](./README.md)

This directory provides Ain one-sided communication samples. Ain targets the operator Kernel side and issues `Put`/`Get`/`Signal` one-sided operations over symmetric windows through the `AscendC::Ain` template.

## Sample List

| Sample | Description |
| --- | --- |
| [01_basic_ring](./01_basic_ring/README_en.md) | Demonstrates point-to-point one-sided `Put`/`Get` communication over the AIN interface in a multi-card ring scenario, with synchronization and result verification through `AinBarrierSession`. |

## Usage Notes

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.

See the README in each sample directory for build commands, expected results, and notes.
