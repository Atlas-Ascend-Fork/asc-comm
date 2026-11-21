# Hcomm Samples

English | [简体中文](./README.md)

This directory provides Hcomm point-to-point communication samples, covering ordinary interfaces, batch interfaces, multi-SGE scattered read/write, and functional and performance tests over the RoCE path.

## Sample List

| Sample | Description |
| --- | --- |
| [07_hcomm_roce_normal_write_read](./07_hcomm_roce_normal_write_read/README_en.md) | Demonstrates AIV Kernel invoking ordinary `WriteNbi`/`ReadNbi` over the RoCE path with MPI-managed processes: deferred tasks are triggered by a single `Commit`, and results are verified after `Drain`. |
| [08_hcomm_roce_batch_write_read](./08_hcomm_roce_batch_write_read/README_en.md) | Demonstrates RoCE batch interfaces with MPI-managed processes: `WriteNbi`/`ReadNbi` are posted consecutively on a `MakeBatchHandle` handle, triggered by `BatchCommit`, and verified on a ring topology. |
| [09_hcomm_roce_multi_sge](./09_hcomm_roce_multi_sge/README_en.md) | Demonstrates RoCE batch multi-SGE interfaces with MPI-managed processes: each `WriteNbi`/`ReadNbi` call carries multiple scattered SGEs, committed in groups and verified. |

## Usage Notes

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library.
- The RoCE samples (07-09) manage processes with MPI. Configure the MPI environment first and launch them with `mpirun`; see each sample README for details.

See the README in each sample directory for build commands, expected results, and notes.
