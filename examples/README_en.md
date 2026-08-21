# asc-comm Samples

This directory contains usage samples for asc-comm APIs.

## Sample List

| Sample | Description | Supported Products |
| --- | --- | --- |
| [ain/basic_ring](./ain/basic_ring/README_en.md) | Demonstrates AIV Kernel invoking `Put`/`Get` over the AIN interface for point-to-point one-sided communication in a multi-card ring scenario, with synchronization and result verification through `AinBarrierSession`. | Ascend 950PR / Ascend 950DT |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README_en.md) | Demonstrates AIV Kernel invoking `Hcomm::WriteNbi` and `Hcomm::ReadNbi` over the URMA path in a multi-card scenario, with communication result verification. | Ascend 950PR / Ascend 950DT |
| [one_multi_path](./one_multi_path/README_en.md) | Queries a `UB_MEM` link, creates one path/multi path channels for every peer, processes peers serially, and concurrently moves and verifies each peer's remote data over two streams. | Ascend 950PR / Ascend 950DT |
| [simt_write](./simt_write/README.md) | Demonstrates and validates SIMT URMA `WriteNbi` and `WriteValueNbi` with immediate and batched submission. | Ascend 950PR / Ascend 950DT |
| [simt_write_perf](./simt_write_perf/README.md) | Measures issue overhead and bandwidth for SIMT URMA `WriteNbi` and `WriteValueNbi` under immediate and deferred publication. | Ascend 950PR / Ascend 950DT |
| [simt_notify_atomic](./simt_notify_atomic/README_en.md) | Demonstrates and validates SIMT URMA `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` with immediate and batched submission. | Ascend 950PR / Ascend 950DT |
| [simt_notify_atomic_perf](./simt_notify_atomic_perf/README_en.md) | Measures issue latency and completion bandwidth for SIMT URMA `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`. | Ascend 950PR / Ascend 950DT |

## simt_write

`simt_write` validates SIMT URMA `WriteNbi` and `WriteValueNbi`, covering isolated interface cases plus serialized and batch-last submission scenarios.

See [simt_write/README.md](./simt_write/README.md) for build and run instructions.

## simt_write_perf

`simt_write_perf` measures SIMT URMA `WriteNbi` and `WriteValueNbi` performance, reporting issue overhead and end-to-end latency with payload sweeps.

See [simt_write_perf/README.md](./simt_write_perf/README.md) for build and run instructions.

## simt_notify_atomic

`simt_notify_atomic` validates SIMT URMA `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`. It includes three isolated interface cases plus serialized and batch-last submission scenarios.

See [simt_notify_atomic/README_en.md](./simt_notify_atomic/README_en.md) for build and run instructions.

## simt_notify_atomic_perf

`simt_notify_atomic_perf` measures SIMT URMA `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` performance, reporting issue latency and completion bandwidth.

See [simt_notify_atomic_perf/README_en.md](./simt_notify_atomic_perf/README_en.md) for performance testing and result aggregation.

## Runtime Constraints
- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required for runtime execution; single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library at link time.
