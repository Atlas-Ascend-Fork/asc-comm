# asc-comm Samples

English | [简体中文](./README.md)

This directory contains usage samples for asc-comm APIs.

## Sample List

| Sample | Description | Supported Products |
| --- | --- | --- |
| [ain/basic_ring](./ain/basic_ring/README_en.md) | Demonstrates AIV Kernel invoking `Put`/`Get` over the AIN interface for point-to-point one-sided communication in a multi-card ring scenario, with synchronization and result verification through `AinBarrierSession`. | Ascend 950PR / Ascend 950DT |
| [hcomm_batch_write](./hcomm_batch_write/README_en.md) | Demonstrates multiple URMA channels sharing a Jetty and submitting cross-peer writes through `MakeBatchHandle`, `GetHandleRef`, `BatchCommit`, and `Drain`. | Ascend 950PR / Ascend 950DT |
| [hcomm_jetty_write](./hcomm_jetty_write/README_en.md) | Demonstrates an AIV Kernel submitting WQEs directly to the Jetty SQ through `HcommJetty::Write` and `HcommJetty::WriteValue` in a multi-card scenario, with `Drain` used for completion and verification. | Ascend 950PR / Ascend 950DT |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README_en.md) | Demonstrates AIV Kernel invoking `Hcomm::WriteNbi` and `Hcomm::ReadNbi` over the URMA path in a multi-card scenario, with communication result verification. | Ascend 950PR / Ascend 950DT |
| [one_multi_path](./one_multi_path/README_en.md) | Queries a `UB_MEM` link, creates one path/multi path channels for every peer, processes peers serially, and concurrently moves and verifies each peer's remote data over two streams. | Ascend 950PR / Ascend 950DT |
| [simt_urma](./simt_urma/README_en.md) | Demonstrates and validates every SIMT URMA interface: `WriteNbi`, `WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`, with immediate and batched submission. | Ascend 950PR / Ascend 950DT |
| [simt_urma_perftest](./simt_urma_perftest/README_en.md) | Measures issue latency and completion bandwidth for those five SIMT URMA interfaces under immediate and deferred publication. | Ascend 950PR / Ascend 950DT |

## simt_urma

`simt_urma` validates SIMT URMA `WriteNbi`, `WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`, covering isolated interface cases plus serialized and batch-last submission across ten modes.

See [simt_urma/README_en.md](./simt_urma/README_en.md) for build and run instructions.

## simt_urma_perftest

`simt_urma_perftest` measures those five interfaces, reporting issue latency and completion bandwidth, with payload sweeps for `write` and `notify`.

See [simt_urma_perftest/README_en.md](./simt_urma_perftest/README_en.md) for performance testing instructions.

## Runtime Constraints
- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required for runtime execution; single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library at link time.
