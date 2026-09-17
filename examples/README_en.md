# asc-comm Samples

English | [简体中文](./README.md)

This directory contains usage samples for asc-comm APIs.

## Sample List

| Sample | Description | Supported Products |
| --- | --- | --- |
| [aicore/ain/01_basic_ring](./aicore/ain/01_basic_ring/README_en.md) | Demonstrates AIV Kernel invoking `Put`/`Get` over the AIN interface for point-to-point one-sided communication in a multi-card ring scenario, with synchronization and result verification through `AinBarrierSession`. | Ascend 950PR / Ascend 950DT |
| [aicore/hcomm/01_hcomm_write_read_nbi](./aicore/hcomm/01_hcomm_write_read_nbi/README_en.md) | Demonstrates AIV Kernel invoking `Hcomm::WriteNbi` and `Hcomm::ReadNbi` over the URMA path in a multi-card scenario, with communication result verification. | Ascend 950PR / Ascend 950DT |
| [aicore/hcomm/02_hcomm_batch_write](./aicore/hcomm/02_hcomm_batch_write/README_en.md) | Demonstrates multiple URMA channels sharing a Jetty and submitting cross-peer writes through `MakeBatchHandle`, `GetHandleRef`, `BatchCommit`, and `Drain`. | Ascend 950PR / Ascend 950DT |
| [aicore/hcomm/03_one_multi_path](./aicore/hcomm/03_one_multi_path/README_en.md) | Queries a `UB_MEM` link, creates one path/multi path channels for every peer, processes peers serially, and concurrently moves and verifies each peer's remote data over two streams. | Ascend 950PR / Ascend 950DT |
| [aicore/hcomm/04_simt_urma](./aicore/hcomm/04_simt_urma/README_en.md) | Demonstrates and validates every SIMT URMA interface: `WriteNbi`, `WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`, with immediate and batched submission. | Ascend 950PR / Ascend 950DT |
| [aicore/hcomm/05_simt_urma_perftest](./aicore/hcomm/05_simt_urma_perftest/README_en.md) | Measures issue latency and completion bandwidth for those five SIMT URMA interfaces under immediate and deferred publication. | Ascend 950PR / Ascend 950DT |
| [hcomm_jetty_write](./hcomm_jetty_write/README_en.md) | Demonstrates an AIV Kernel submitting WQEs directly to the Jetty SQ through `HcommJetty::Write` and `HcommJetty::WriteValue` in a multi-card scenario, with `Drain` used for completion and verification. | Ascend 950PR / Ascend 950DT |
| [ccu/ccu_direct](./ccu/ccu_direct/01_allgather/README_en.md) | Demonstrates AllGather and other collective communication operations using HCCL communicators and CCU dataplane interfaces with direct `<<<>>>` invocation. | Ascend 950PR/Ascend 950DT |

See the README in each sample directory for build and run instructions.

## Runtime Constraints

- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required for runtime execution; single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library.
