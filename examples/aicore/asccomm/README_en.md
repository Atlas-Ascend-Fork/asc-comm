# asccomm Samples

English | [简体中文](./README.md)

This directory provides asccomm point-to-point communication samples, covering ordinary interfaces, batch interfaces, and functional and performance tests over the AIV direct-driven URMA and SIMT URMA paths.

## Sample List

| Sample | Description |
| --- | --- |
| [01_asccomm_urma_write_read_nbi](./01_asccomm_urma_write_read_nbi/README_en.md) | Demonstrates AIV Kernel invoking `asccomm::asccomm_write_nbi` and `asccomm::asccomm_read_nbi` over the URMA path in a multi-card scenario, with communication result verification. |
| [02_asccomm_urma_batch_write](./02_asccomm_urma_batch_write/README_en.md) | Demonstrates multiple URMA channels sharing a Jetty and submitting cross-peer writes through `asccomm_make_batch_handle`, `asccomm_get_handle_ref`, `asccomm_batch_commit`, and `asccomm_drain`. |
| [03_asccomm_urma_one_multi_path](./03_asccomm_urma_one_multi_path/README_en.md) | Queries a `UB_MEM` link, creates one path/multi path channels for every peer, processes peers serially, and concurrently moves and verifies each peer's remote data over two streams. |
| [04_simt_asccomm_urma](./04_simt_asccomm_urma/README_en.md) | Demonstrates and validates every SIMT URMA interface: `asccomm_write_nbi`, `asccomm_write_value_nbi`, `asccomm_write_with_notify_nbi`, `asccomm_atomic_faa_nbi`, and `asccomm_atomic_cas_nbi`, with immediate and batched submission. |
| [05_simt_asccomm_urma_perftest](./05_simt_asccomm_urma_perftest/README_en.md) | Measures issue latency and completion bandwidth for those five SIMT URMA interfaces under immediate and deferred publication. |
| [06_simt_asccomm_urma_batch_perf](./06_simt_asccomm_urma_batch_perf/README_en.md) | Validates six SIMT URMA APIs and compares issue cost for ExplicitBatch and GroupBatch. |
| [07_asccomm_roce_normal_write_read](./07_asccomm_roce_normal_write_read/README_en.md) | Demonstrates AIV Kernel invoking ordinary RoCE `asccomm_write_nbi`/`asccomm_read_nbi` in MPI-managed processes: deferred requests are triggered by `asccomm_commit`, and `asccomm_drain` waits for completion and result validation. |
| [08_asccomm_roce_batch_write_read](./08_asccomm_roce_batch_write_read/README_en.md) | Demonstrates RoCE batch interfaces in MPI-managed processes: read/write requests are posted on an `asccomm_make_batch_handle` handle, triggered by `asccomm_batch_commit`, and verified on a ring topology. |
| [09_asccomm_roce_multi_sge](./09_asccomm_roce_multi_sge/README_en.md) | Demonstrates RoCE batch multi-SGE interfaces in MPI-managed processes: each call carries multiple scattered SGEs, committed in groups and verified. |
| [10_asccomm_roce_atomic_inline](./10_asccomm_roce_atomic_inline/README_en.md) | Demonstrates ordinary RoCE interfaces in MPI-managed processes: `asccomm_atomic_faa_nbi`, `asccomm_atomic_cas_nbi`, and `asccomm_write_value_nbi` are posted and committed together before completion. |
| [11_asccomm_roce_api_perf](./11_asccomm_roce_api_perf/README_en.md) | RoCE interface performance test: measures latency and throughput of batch and normal APIs at different payload sizes through `--mode batch/normal`, covering issue, commit, completion, and whole-round device time. |
| [12_asccomm_urma_normal_api_perf](./12_asccomm_urma_normal_api_perf/README_en.md) | URMA normal interface performance test: measures latency and bandwidth of `asccomm_write_nbi`/`asccomm_read_nbi`/`asccomm_write_with_notify_nbi`, supporting p2p/ring topologies and inline/final commit modes. |
| [13_asccomm_urma_batch_api_perf](./13_asccomm_urma_batch_api_perf/README_en.md) | URMA batch interface performance test: measures batch posting, `asccomm_batch_commit`, and `asccomm_drain` latency and bandwidth across `(len, batch_cap)` tiers, supporting p2p/ring topologies and CQE policy tuning. |

## Usage Notes

- The samples support Ascend 950PR&950DT products. See each sample README for the exact CANN version requirement.
- At least two NPUs are required to run the samples. Single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `asccomm` library.

See the README in each sample directory for build commands, expected results, and notes.
