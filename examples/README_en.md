# asc-comm Samples

This directory contains usage samples for asc-comm APIs.

## Sample List

| Sample | Description | Supported Products |
| --- | --- | --- |
| [ain/basic_ring](./ain/basic_ring/README_en.md) | Demonstrates AIV Kernel invoking `Put`/`Get` over the AIN interface for point-to-point one-sided communication in a multi-card ring scenario, with synchronization and result verification through `AinBarrierSession`. | Ascend 950PR / Ascend 950DT |
| [hcomm_batch_write](./hcomm_batch_write/README_en.md) | Demonstrates multiple URMA channels sharing a Jetty and submitting cross-peer writes through `MakeBatchHandle`, `GetHandleRef`, `BatchCommit`, and `Drain`. | Ascend 950PR / Ascend 950DT |
| [hcomm_handle_write_perf](./hcomm_handle_write_perf/README_en.md) | Uses the DeepEP `handle.hpp` `HcommPeer` and `HcommJetty` interfaces directly to measure average device-cycle latency for 1000 consecutive 256-byte WRITE submissions, then verifies completion. | Ascend 950PR / Ascend 950DT |
| [hcomm_jetty_write_perf](./hcomm_jetty_write_perf/README_en.md) | Measures average device-cycle latency over 1000 AIV Jetty `HcommJetty::Put` submissions, then completes and verifies them with `Drain`. | Ascend 950PR / Ascend 950DT |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README_en.md) | Demonstrates AIV Kernel invoking `Hcomm::WriteNbi` and `Hcomm::ReadNbi` over the URMA path in a multi-card scenario, with communication result verification. | Ascend 950PR / Ascend 950DT |
| [one_multi_path](./one_multi_path/README_en.md) | Queries a `UB_MEM` link, creates one path/multi path channels for every peer, processes peers serially, and concurrently moves and verifies each peer's remote data over two streams. | Ascend 950PR / Ascend 950DT |
| [simt_urma](./simt_urma/README_en.md) | Demonstrates and validates every SIMT URMA interface: `WriteNbi`, `WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`, with immediate and batched submission. | Ascend 950PR / Ascend 950DT |
| [simt_urma_perftest](./simt_urma_perftest/README_en.md) | Measures issue latency and completion bandwidth for those five SIMT URMA interfaces under immediate and deferred publication. | Ascend 950PR / Ascend 950DT |

## simt_urma

`simt_urma` validates SIMT URMA `WriteNbi`, `WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`, covering isolated interface cases plus serialized and batch-last submission across ten modes.

See [simt_urma/README_en.md](./simt_urma/README_en.md) for build and run instructions.

## hcomm_jetty_write_perf

`hcomm_jetty_write_perf` submits 1000 Jetty `Put` calls per peer and prints their average device-cycle
latency. The timed interval surrounds only the submission loop. `Drain` runs afterward to complete and verify
the communication.

See [hcomm_jetty_write_perf/README_en.md](./hcomm_jetty_write_perf/README_en.md) for performance testing instructions.

## hcomm_handle_write_perf

`hcomm_handle_write_perf` keeps `handle.hpp` and its direct DeepEP dependencies inside the sample. It uses `HcommPeer` and `HcommJetty` to submit 1000 consecutive 256-byte WRITEs per peer, measures only the submission loop, and calls `drain` afterward to complete and verify the communication.

See [hcomm_handle_write_perf/README_en.md](./hcomm_handle_write_perf/README_en.md) for performance testing instructions.

## simt_urma_perftest

`simt_urma_perftest` measures those five interfaces, reporting issue latency and completion bandwidth, with payload sweeps for `write` and `notify`.

See [simt_urma_perftest/README_en.md](./simt_urma_perftest/README_en.md) for performance testing instructions.

## Runtime Constraints
- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required for runtime execution; single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library at link time.
